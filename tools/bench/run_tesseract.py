#!/usr/bin/env python3
"""Run the Tesseract variants of the OCR benchmark.

The preprocessing mirrors src/TesseractOcr.cpp so the numbers reflect what the
application actually does:

    grayscale -> optional nearest-neighbour upscale -> invert dark themes
    -> Otsu binarise -> BMP -> tesseract (LSTM, one thread, word TSV)

Variants differ in page segmentation mode, upscale factor, whether the
binarisation step is applied, the character whitelist and the traineddata
model. The whitelist matters a lot: with an LSTM model a whitelist that omits
the space makes Tesseract drop the spaces and merge whole lines into one token.

Usage:
    python tools/bench/run_tesseract.py
    python tools/bench/run_tesseract.py --only no-wl-100-11 --reps 3
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
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
BENCH = ROOT / "tools" / "bench"
MANIFEST = BENCH / "corpus" / "manifest.json"
RESULTS = BENCH / "results"

TESSERACT = Path(r"D:\msys64\ucrt64\bin\tesseract.exe")
TESSDATA_FAST = Path(r"D:\msys64\ucrt64\share\tessdata")
TESSDATA_BEST = BENCH / "models" / "tessdata-best"

LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'"

# name, preprocessing, scale %, psm, whitelist mode, model
VARIANTS: list[tuple[str, str, int, int, str, str]] = [
    ("wl-100-11", "app", 100, 11, "letters", "fast"),     # previous application default
    ("wls-100-11", "app", 100, 11, "letters-space", "fast"),
    ("no-wl-100-11", "app", 100, 11, "none", "fast"),
    ("no-wl-100-6", "app", 100, 6, "none", "fast"),
    ("no-wl-100-3", "app", 100, 3, "none", "fast"),
    ("no-wl-150-6", "app", 150, 6, "none", "fast"),
    ("gray-075-6", "gray", 75, 6, "none", "fast"),
    ("gray-100-6", "gray", 100, 6, "none", "fast"),
    ("gray-150-6", "gray", 150, 6, "none", "fast"),
    ("gray-150-11", "gray", 150, 11, "none", "fast"),
    ("best-100-6", "app", 100, 6, "none", "best"),
    ("best-100-11", "app", 100, 11, "none", "best"),
]


def otsu(gray: np.ndarray) -> int:
    hist = np.bincount(gray.ravel(), minlength=256).astype(np.float64)
    total = gray.size
    if total == 0:
        return 128
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


def prepare(path: Path, mode: str, scale: int) -> tuple[Image.Image, float]:
    started = time.perf_counter()
    img = Image.open(path).convert("L")
    if scale != 100:
        target = (max(1, img.width * scale // 100), max(1, img.height * scale // 100))
        if target[0] * target[1] <= 24 * 1024 * 1024:
            # Nearest-neighbour keeps upscaled glyph edges sharp; shrinking needs
            # a proper filter or thin strokes alias away.
            img = img.resize(target, Image.NEAREST if scale > 100 else Image.LANCZOS)

    gray = np.asarray(img, dtype=np.uint8)
    step_x = max(1, gray.shape[1] // 160)
    step_y = max(1, gray.shape[0] // 90)
    sample = gray[::step_y, ::step_x]
    if sample.size and float(sample.mean()) < 110:
        gray = 255 - gray

    if mode == "app":
        threshold = otsu(gray)
        gray = np.where(gray < threshold, 0, 255).astype(np.uint8)

    elapsed = (time.perf_counter() - started) * 1000.0
    return Image.fromarray(gray, mode="L"), elapsed


def whitelist_args(mode: str) -> list[str]:
    if mode == "letters":
        return ["-c", f"tessedit_char_whitelist={LETTERS}"]
    if mode == "letters-space":
        return ["-c", f"tessedit_char_whitelist={LETTERS} "]
    return []


def run_tesseract(image: Path, psm: int, wl: str, model: str) -> tuple[list[dict], int]:
    tessdata = TESSDATA_BEST if model == "best" else TESSDATA_FAST
    args = [
        str(TESSERACT), str(image), "stdout",
        "-l", "eng",
        "--psm", str(psm),
        "--oem", "1",
        *whitelist_args(wl),
        "-c", "textord_tabfind_find_tables=0",
        "-c", "textord_tablefind_recognize_tables=0",
        "-c", "tessedit_do_invert=0",
        "--tessdata-dir", str(tessdata),
        "tsv",
    ]
    env = dict(os.environ, OMP_THREAD_LIMIT="1", OMP_NUM_THREADS="1")
    started = time.perf_counter()
    proc = subprocess.run(args, capture_output=True, env=env, timeout=300)
    elapsed = int((time.perf_counter() - started) * 1000.0)

    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.decode("utf-8", "replace")[-800:])

    words = []
    text = proc.stdout.decode("utf-8", "replace")
    for line in text.splitlines()[1:]:
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
    return words, elapsed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reps", type=int, default=1)
    parser.add_argument("--only", default="", help="comma separated variant names")
    parser.add_argument("--out", default=str(RESULTS / "tesseract.json"))
    args = parser.parse_args()

    if not TESSERACT.is_file():
        raise SystemExit(f"tesseract not found: {TESSERACT}")

    wanted = {v for v in args.only.split(",") if v} or None
    variants = [v for v in VARIANTS if wanted is None or v[0] in wanted]
    pages = json.loads(MANIFEST.read_text(encoding="utf-8"))

    RESULTS.mkdir(parents=True, exist_ok=True)
    version = subprocess.run([str(TESSERACT), "--version"], capture_output=True,
                             text=True, encoding="utf-8", errors="replace").stdout.splitlines()[0]

    runs = []
    workdir = Path(tempfile.mkdtemp(prefix="wlbench-tess-"))
    try:
        for name, mode, scale, psm, wl, model in variants:
            if model == "best" and not (TESSDATA_BEST / "eng.traineddata").is_file():
                print(f"skip {name}: missing {TESSDATA_BEST / 'eng.traineddata'}")
                continue
            for page in pages:
                png = ROOT / page["png"]
                prepared, prep_ms = prepare(png, mode, scale)
                bmp = workdir / f"{page['page']}-{name}.bmp"
                prepared.save(bmp, "BMP")
                for rep in range(args.reps):
                    words, ocr_ms = run_tesseract(bmp, psm, wl, model)
                    runs.append({
                        "page": page["page"],
                        "variant": name,
                        "prep": mode,
                        "scale": scale,
                        "psm": psm,
                        "whitelist": wl,
                        "model": model,
                        "rep": rep,
                        "prep_ms": round(prep_ms, 1),
                        "ocr_ms": ocr_ms,
                        "total_ms": round(prep_ms + ocr_ms, 1),
                        "tokens": len(words),
                        "words": words,
                    })
                    print(f"{name:<14} {page['page']:<9} rep{rep} "
                          f"prep={prep_ms:6.0f}ms ocr={ocr_ms:5d}ms tokens={len(words):4d}")
                bmp.unlink(missing_ok=True)
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    # Keep earlier variants in the same file so a partial rerun (--only) does not
    # silently drop results the report still refers to.
    out_path = Path(args.out)
    kept = []
    if out_path.is_file() and wanted is not None:
        try:
            previous = json.loads(out_path.read_text(encoding="utf-8"))
            kept = [r for r in previous.get("runs", []) if r.get("variant") not in wanted]
        except json.JSONDecodeError:
            kept = []

    payload = {"backend": "tesseract", "engine": version, "runs": kept + runs}
    out_path.write_text(json.dumps(payload, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"\nwrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
