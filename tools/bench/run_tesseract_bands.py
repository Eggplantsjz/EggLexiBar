#!/usr/bin/env python3
"""Tesseract, but split across CPU cores.

Tesseract is single-threaded, and one process on a 3840x2160 screen grab costs
2-7 seconds - far longer than the 1.5 second scan interval WordLens wants. The
obvious fix is to run several processes over horizontal slices of the same
frame and merge their word boxes.

Slicing at fixed offsets would cut text lines in half, so the cuts are chosen on
completely blank rows near the ideal boundaries. Because no glyph is ever
divided, the merged result should be identical to the single-process output
while wall-clock time drops roughly with the core count.

Usage:
    python tools/bench/run_tesseract_bands.py
    python tools/bench/run_tesseract_bands.py --only w4-11
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
BENCH = ROOT / "tools" / "bench"
MANIFEST = BENCH / "corpus" / "manifest.json"
RESULTS = BENCH / "results"

TESSERACT = Path(r"D:\msys64\ucrt64\bin\tesseract.exe")
TESSDATA = Path(r"D:\msys64\ucrt64\share\tessdata")

VARIANTS: list[tuple[str, int, int, str]] = [
    ("w1-11", 1, 11, "app"),
    ("w2-11", 2, 11, "app"),
    ("w4-11", 4, 11, "app"),
    ("w4-6", 4, 6, "app"),
    ("w6-6", 6, 6, "app"),
    ("w1-g6", 1, 6, "gray"),
    ("w2-g6", 2, 6, "gray"),
    ("w4-g6", 4, 6, "gray"),
]


def otsu(gray: np.ndarray) -> int:
    hist = np.bincount(gray.ravel(), minlength=256).astype(np.float64)
    total = gray.size
    levels = np.arange(256, dtype=np.float64)
    weight_bg = np.cumsum(hist)
    sum_all = float((levels * hist).sum())
    sum_bg = np.cumsum(levels * hist)
    valid = (weight_bg > 0) & (weight_bg < total)
    if not valid.any():
        return 128
    mean_bg = sum_bg[valid] / weight_bg[valid]
    mean_fg = (sum_all - sum_bg[valid]) / (total - weight_bg[valid])
    variance = weight_bg[valid] * (total - weight_bg[valid]) * (mean_bg - mean_fg) ** 2
    return int(np.argmax(variance))


def prepare(path: Path, mode: str = "app") -> tuple[np.ndarray, float]:
    started = time.perf_counter()
    gray = np.asarray(Image.open(path).convert("L"), dtype=np.uint8)
    sample = gray[:: max(1, gray.shape[0] // 90), :: max(1, gray.shape[1] // 160)]
    if float(sample.mean()) < 110:
        gray = 255 - gray
    if mode != "app":
        return gray, (time.perf_counter() - started) * 1000.0
    threshold = otsu(gray)
    binary = np.where(gray < threshold, np.uint8(0), np.uint8(255))
    return binary, (time.perf_counter() - started) * 1000.0


def split_rows(binary: np.ndarray, workers: int) -> list[tuple[int, int]]:
    """Split at blank rows so no text line is cut in half."""
    if workers <= 1:
        return [(0, binary.shape[0])]

    ink = (binary < 128).sum(axis=1)
    height = binary.shape[0]
    cuts = [0]
    for i in range(1, workers):
        ideal = height * i // workers
        window = max(8, height // (workers * 2))
        lo = max(cuts[-1] + 1, ideal - window)
        hi = min(height - 1, ideal + window)
        rows = np.arange(lo, hi)
        if rows.size == 0:
            cuts.append(ideal)
            continue
        best = rows[np.argmin(ink[lo:hi])]
        cuts.append(int(best))
    cuts.append(height)

    bands = []
    for i in range(len(cuts) - 1):
        top = max(0, cuts[i] - 2)
        bottom = min(height, cuts[i + 1] + 2)
        if bottom - top >= 8:
            bands.append((top, bottom))
    return bands


def run_tesseract(image: Path, psm: int) -> tuple[list[dict], int]:
    args = [
        str(TESSERACT), str(image), "stdout",
        "-l", "eng", "--psm", str(psm), "--oem", "1",
        "-c", "textord_tabfind_find_tables=0",
        "-c", "textord_tablefind_recognize_tables=0",
        "-c", "tessedit_do_invert=0",
        "--tessdata-dir", str(TESSDATA),
        "tsv",
    ]
    env = dict(os.environ, OMP_THREAD_LIMIT="1", OMP_NUM_THREADS="1")
    started = time.perf_counter()
    proc = subprocess.run(args, capture_output=True, env=env, timeout=300)
    elapsed = (time.perf_counter() - started) * 1000.0
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.decode("utf-8", "replace")[-800:])

    words = []
    for line in proc.stdout.decode("utf-8", "replace").splitlines()[1:]:
        fields = line.split("\t")
        if len(fields) < 12 or fields[0] != "5":
            continue
        try:
            conf = float(fields[10])
        except ValueError:
            continue
        token = fields[11].strip()
        if not token or conf < 0:
            continue
        words.append({
            "text": token,
            "conf": round(conf, 1),
            "box": [int(fields[6]), int(fields[7]), int(fields[8]), int(fields[9])],
        })
    return words, int(elapsed)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reps", type=int, default=2)
    parser.add_argument("--only", default="")
    parser.add_argument("--out", default=str(RESULTS / "tesseract-bands.json"))
    args = parser.parse_args()

    wanted = {v for v in args.only.split(",") if v} or None
    variants = [v for v in VARIANTS if wanted is None or v[0] in wanted]
    pages = json.loads(MANIFEST.read_text(encoding="utf-8"))
    RESULTS.mkdir(parents=True, exist_ok=True)

    runs = []
    workdir = Path(tempfile.mkdtemp(prefix="wlbench-bands-"))
    try:
        for name, workers, psm, mode in variants:
            for page in pages:
                binary, prep_ms = prepare(ROOT / page["png"], mode)
                bands = split_rows(binary, workers)

                strips: list[tuple[int, Path]] = []
                for index, (top, bottom) in enumerate(bands):
                    strip_path = workdir / f"{page['page']}-{name}-{index}.bmp"
                    Image.fromarray(binary[top:bottom], "L").save(strip_path, "BMP")
                    strips.append((top, strip_path))

                for rep in range(args.reps):
                    started = time.perf_counter()
                    with ThreadPoolExecutor(max_workers=len(strips)) as pool:
                        futures = [pool.submit(run_tesseract, path, psm) for _, path in strips]
                        results = [f.result() for f in futures]
                    wall_ms = (time.perf_counter() - started) * 1000.0

                    words: list[dict] = []
                    busy_ms = 0
                    for (top, _), (band_words, band_ms) in zip(strips, results):
                        busy_ms += band_ms
                        for word in band_words:
                            word["box"][1] += top
                            word["box"][3] += top
                            words.append(word)

                    runs.append({
                        "page": page["page"],
                        "variant": name,
                        "prep": mode,
                        "workers": workers,
                        "psm": psm,
                        "bands": len(strips),
                        "rep": rep,
                        "prep_ms": round(prep_ms, 1),
                        "ocr_ms": round(wall_ms, 1),
                        "cpu_ms": busy_ms,
                        "total_ms": round(prep_ms + wall_ms, 1),
                        "tokens": len(words),
                        "words": words,
                    })
                    print(f"{name:<6} {page['page']:<9} rep{rep} bands={len(strips)} "
                          f"prep={prep_ms:6.0f}ms wall={wall_ms:7.0f}ms "
                          f"cpu={busy_ms:7.0f}ms tokens={len(words):4d}")

                for _, strip_path in strips:
                    strip_path.unlink(missing_ok=True)
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    payload = {"backend": "tesseract", "engine": "tesseract (band parallel)", "runs": runs}
    out_path = Path(args.out)
    kept = []
    if out_path.is_file() and wanted is not None:
        try:
            previous = json.loads(out_path.read_text(encoding="utf-8"))
            kept = [r for r in previous.get("runs", []) if r.get("variant") not in wanted]
        except json.JSONDecodeError:
            kept = []
    payload["runs"] = kept + runs
    out_path.write_text(json.dumps(payload, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"\nwrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
