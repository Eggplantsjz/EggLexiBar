#!/usr/bin/env python3
"""Run the RapidOCR (PP-OCR ONNX) variants of the OCR benchmark.

RapidOCR is a self-contained PP-OCR pipeline: a DBNet text detector, an
optional direction classifier and a CRNN/SVTR recogniser executed by
onnxruntime. It detects text lines instead of segmenting the page, which is why
it handles coloured UI text and monospace code well.

The detector is the expensive stage. RapidOCR's default (`limit_type: min`,
736) only *upscales* small inputs, so a 3840x2160 screenshot is passed to DBNet
at full resolution and detection alone takes 3.4 s. Capping the long side
(`limit_type: max`) fixes that; recognition still crops from the original image,
so the recogniser keeps its full detail.

Variants (all with the angle classifier off and one ONNX thread, both measured
faster than the defaults on this CPU):

    det736     long side capped at 736
    det1280    long side capped at 1280
    det1920    long side capped at 1920

Usage:
    python tools/bench/run_rapidocr.py
    python tools/bench/run_rapidocr.py --only det1280 --reps 3
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
BENCH = ROOT / "tools" / "bench"
MANIFEST = BENCH / "corpus" / "manifest.json"
RESULTS = BENCH / "results"

VARIANTS: list[tuple[str, dict]] = [
    ("det736", {"limit_side_len": 736, "limit_type": "max"}),
    ("det1280", {"limit_side_len": 1280, "limit_type": "max"}),
    ("det1920", {"limit_side_len": 1920, "limit_type": "max"}),
]

COMMON = {"use_angle_cls": False, "intra_op_num_threads": 1}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reps", type=int, default=2)
    parser.add_argument("--only", default="", help="comma separated variant names")
    parser.add_argument("--out", default=str(RESULTS / "rapidocr.json"))
    args = parser.parse_args()

    try:
        import rapidocr_onnxruntime as rapidocr
    except ImportError:
        raise SystemExit("rapidocr_onnxruntime is missing; pip install -r tools/bench/requirements.txt")

    import onnxruntime

    wanted = {v for v in args.only.split(",") if v} or None
    variants = [v for v in VARIANTS if wanted is None or v[0] in wanted]
    pages = json.loads(MANIFEST.read_text(encoding="utf-8"))
    RESULTS.mkdir(parents=True, exist_ok=True)

    runs = []
    for name, det in variants:
        started = time.perf_counter()
        engine = rapidocr.RapidOCR(**COMMON, **det)
        load_ms = (time.perf_counter() - started) * 1000.0
        print(f"--- {name}: engine load {load_ms:.0f} ms")

        for page in pages:
            image = np.asarray(Image.open(ROOT / page["png"]).convert("RGB"))
            for rep in range(args.reps):
                started = time.perf_counter()
                result, _elapse = engine(image)
                ocr_ms = (time.perf_counter() - started) * 1000.0

                words = []
                for item in result or []:
                    box, text, score = item[0], item[1], item[2]
                    # PP-OCR returns one entry per text line; score.py splits
                    # on whitespace and punctuation, so keep the raw line.
                    words.append({
                        "text": str(text).strip(),
                        "conf": round(float(score) * 100.0, 1),
                        "box": [int(min(p[0] for p in box)), int(min(p[1] for p in box)),
                                int(max(p[0] for p in box)), int(max(p[1] for p in box))],
                    })

                runs.append({
                    "page": page["page"],
                    "variant": name,
                    "det_limit": det["limit_side_len"],
                    "rep": rep,
                    "prep_ms": 0.0,
                    "ocr_ms": round(ocr_ms, 1),
                    "total_ms": round(ocr_ms, 1),
                    "tokens": len(words),
                    "words": words,
                })
                print(f"{name:<8} {page['page']:<9} rep{rep} "
                      f"ocr={ocr_ms:7.0f}ms lines={len(words):4d}")

    payload = {
        "backend": "rapidocr",
        "engine": f"rapidocr_onnxruntime onnxruntime {onnxruntime.__version__}",
        "runs": runs,
    }
    Path(args.out).write_text(json.dumps(payload, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"\nwrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
