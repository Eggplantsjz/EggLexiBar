#!/usr/bin/env python3
"""Score every backend recorded in tools/bench/results/*.json.

Ground truth comes from the corpus generator, which extracted the tokens from
the HTML it rendered, so no OCR result is ever used as truth.

Metrics per page and variant:

    token P/R/F1   bag-of-words over lowercased tokens (len >= 3)
    unique R       share of distinct ground-truth words that were read at least
                   once - this is what decides whether a word can enter the bar
    rare R         same, restricted to words outside the 3000 most common
                   English words (the words a learner actually wants)
    long %         share of output tokens longer than 24 characters; a merged
                   line ("everydistributedsystemis...") inflates this and is a
                   direct symptom of the missing-space whitelist bug
    ms             median (prep + OCR) milliseconds per page

Usage:
    python tools/bench/score.py
    python tools/bench/score.py --md results/summary.md
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BENCH = ROOT / "tools" / "bench"
RESULTS = BENCH / "results"

TOKEN_RE = re.compile(r"[A-Za-z]+(?:['\u2019\-][A-Za-z]+)*")
WORD_RE = re.compile(r"^[a-z]+(?:['\-][a-z]+)*$")


def normalise(text: str) -> list[str]:
    out = []
    for raw in TOKEN_RE.findall(text or ""):
        token = raw.replace("\u2019", "'").lower()
        if WORD_RE.match(token) and len(token) >= 3:
            out.append(token)
    return out


def run_tokens(run: dict) -> list[str]:
    """Flatten a run's recognised words into benchmark tokens.

    Tesseract returns one word per TSV row, RapidOCR returns whole lines and
    Windows OCR returns individual words. Normalising through the same regex
    keeps the comparison fair for all three.
    """
    tokens: list[str] = []
    for item in run.get("words", []):
        tokens.extend(normalise(item.get("text", "")))
    return tokens


def multiset(tokens: list[str]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for token in tokens:
        counts[token] = counts.get(token, 0) + 1
    return counts


def score_run(run: dict, truth: dict, common: set[str]) -> dict:
    produced = run_tokens(run)
    produced_counts = multiset(produced)
    truth_counts = multiset(truth["tokens"])

    overlap = sum(min(n, truth_counts[t]) for t, n in produced_counts.items() if t in truth_counts)
    total_truth = sum(truth_counts.values())
    total_produced = sum(produced_counts.values())

    precision = overlap / total_produced if total_produced else 0.0
    recall = overlap / total_truth if total_truth else 0.0
    f1 = 2 * precision * recall / (precision + recall) if precision + recall else 0.0

    truth_unique = set(truth_counts)
    prod_unique = set(produced_counts)
    unique_recall = len(truth_unique & prod_unique) / len(truth_unique) if truth_unique else 0.0
    unique_precision = len(truth_unique & prod_unique) / len(prod_unique) if prod_unique else 0.0

    rare = {w for w in truth_unique if w not in common}
    rare_recall = len(rare & prod_unique) / len(rare) if rare else 0.0

    long_tokens = sum(1 for t in produced if len(t) > 24)
    long_ratio = long_tokens / total_produced if total_produced else 0.0

    return {
        "precision": precision,
        "recall": recall,
        "f1": f1,
        "unique_recall": unique_recall,
        "unique_precision": unique_precision,
        "rare_recall": rare_recall,
        "long_ratio": long_ratio,
        "produced": total_produced,
        "truth": total_truth,
        "ms": run.get("total_ms", 0.0),
        "missing": sorted(rare - prod_unique)[:12],
    }


def median(values: list[float]) -> float:
    return statistics.median(values) if values else 0.0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--md", default="", help="also write a Markdown table here")
    args = parser.parse_args()

    manifest = json.loads((BENCH / "corpus" / "manifest.json").read_text(encoding="utf-8"))
    truths = {p["page"]: json.loads((ROOT / p["truth"]).read_text(encoding="utf-8"))
              for p in manifest}
    common = {line.strip().lower() for line in
              (ROOT / "data" / "common-10000.txt").read_text(encoding="utf-8").splitlines()[:3000]}

    results: dict[tuple[str, str], dict] = {}
    for path in sorted(RESULTS.glob("*.json")):
        if path.name in ("summary.json",):
            continue
        payload = json.loads(path.read_text(encoding="utf-8"))
        backend = payload.get("backend", path.stem)
        for run in payload.get("runs", []):
            key = (backend, run["variant"])
            entry = results.setdefault(key, {"backend": backend, "variant": run["variant"],
                                             "pages": {}, "runs": 0})
            entry["runs"] += 1
            metrics = score_run(run, truths[run["page"]], common)
            entry["pages"].setdefault(run["page"], []).append(metrics)

    if not results:
        raise SystemExit("no results found; run one of the benchmark runners first")

    rows = []
    for (backend, variant), entry in sorted(results.items()):
        page_scores = {}
        for page, runs in entry["pages"].items():
            page_scores[page] = {
                "f1": median([r["f1"] for r in runs]),
                "unique_recall": median([r["unique_recall"] for r in runs]),
                "rare_recall": median([r["rare_recall"] for r in runs]),
                "precision": median([r["precision"] for r in runs]),
                "long_ratio": median([r["long_ratio"] for r in runs]),
                "ms": median([r["ms"] for r in runs]),
                "missing": runs[0]["missing"],
            }
            # Accuracy is identical across reps; the median is just safe.
        rows.append({
            "backend": backend,
            "variant": variant,
            "pages": page_scores,
            "f1": median([p["f1"] for p in page_scores.values()]),
            "unique_recall": median([p["unique_recall"] for p in page_scores.values()]),
            "rare_recall": median([p["rare_recall"] for p in page_scores.values()]),
            "long_ratio": median([p["long_ratio"] for p in page_scores.values()]),
            "ms": median([p["ms"] for p in page_scores.values()]),
        })

    pages = [p["page"] for p in manifest]
    header = (f"{'backend':<10} {'variant':<16} " +
              " ".join(f"{p[:8]:>9}" for p in pages) +
              f" {'F1':>6} {'uniqR':>6} {'rareR':>6} {'long%':>6} {'ms':>7}")
    print(header)
    print("-" * len(header))
    for row in sorted(rows, key=lambda r: (-r["f1"], r["ms"])):
        line = f"{row['backend']:<10} {row['variant']:<16} "
        for page in pages:
            score = row["pages"].get(page)
            line += f"{(score['f1'] * 100 if score else 0):8.1f}% "
        line += (f"{row['f1'] * 100:5.1f}% {row['unique_recall'] * 100:5.1f}% "
                 f"{row['rare_recall'] * 100:5.1f}% {row['long_ratio'] * 100:5.1f}% "
                 f"{row['ms']:7.0f}")
        print(line)

    summary = {"pages": pages, "rows": rows}
    out = RESULTS / "summary.json"
    out.write_text(json.dumps(summary, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"\nwrote {out}")

    if args.md:
        md = ["| backend | variant | " + " | ".join(pages) +
              " | F1 | uniqR | rareR | merged% | ms |",
              "|" + "---|" * (len(pages) + 7)]
        for row in sorted(rows, key=lambda r: (-r["f1"], r["ms"])):
            cells = [row["backend"], row["variant"]]
            for page in pages:
                score = row["pages"].get(page)
                cells.append(f"{score['f1'] * 100:.1f}" if score else "-")
            cells += [f"{row['f1'] * 100:.1f}", f"{row['unique_recall'] * 100:.1f}",
                      f"{row['rare_recall'] * 100:.1f}", f"{row['long_ratio'] * 100:.1f}",
                      f"{row['ms']:.0f}"]
            md.append("| " + " | ".join(cells) + " |")
        Path(args.md).write_text("\n".join(md) + "\n", encoding="utf-8")
        print(f"wrote {args.md}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
