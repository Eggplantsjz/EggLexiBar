# OCR benchmark

Comparing the OCR backends that could power WordLens. The question is narrow:
**read English screen text accurately and fast enough to run every 1.5 s**, not
"OCR a scanned book".

Everything is reproducible from this directory.

## Corpus

`make_corpus.py` authors six pages, renders them with headless Edge at
2560x1440 CSS pixels and `--force-device-scale-factor=1.5`, which produces a
3840x2160 PNG - exactly what a 4K screen looks like at 150% scaling.

| page | what it stresses |
|---|---|
| `article` | 19-22px light-theme prose, headings, quotes |
| `code` | 14px dark-theme monospace, syntax colours, line numbers |
| `ui` | 13px dark UI labels, checkboxes, a small table |
| `doc` | 15px light README with inline code, `state-of-the-art`, URLs |
| `terminal` | 13px dark log lines, timestamps, paths, mixed colours |
| `dense` | 12px file tree and code editor - the hard case |

Because the pages are authored here, the ground truth is exact: the token list
is parsed out of the same HTML that was rendered, never out of an OCR result.
`corpus/truth/*.json` also records each page's *rare* words (outside the 3000
most common English words), which is what a vocabulary tool actually surfaces.

    python tools/bench/make_corpus.py            # render + write truth
    python tools/bench/make_corpus.py --no-render

## Runners

    python tools/bench/run_tesseract.py           # psm / scale / whitelist / model
    python tools/bench/run_tesseract_bands.py     # split the frame across cores
    python tools/bench/run_rapidocr.py            # PP-OCR (DBNet + SVTR via onnxruntime)
    powershell -NoProfile -File tools/bench/run_winocr.ps1

Each runner writes `results/<backend>.json` with one entry per (page, variant,
repetition) containing every recognised word, its confidence, its box and the
preprocessing/OCR timings.

Tesseract-specific variants:

| variant | preprocessing | scale | psm | whitelist | model |
|---|---|---|---|---|---|
| `wl-100-11` | otsu | 100 | 11 | letters | fast |
| `wls-100-11` | otsu | 100 | 11 | letters+space | fast |
| `no-wl-100-11` | otsu | 100 | 11 | none | fast |
| `no-wl-100-6` | otsu | 100 | 6 | none | fast |
| `no-wl-100-3` | otsu | 100 | 3 | none | fast |
| `no-wl-150-6` | otsu | 150 | 6 | none | fast |
| `gray-150-6` | grayscale only | 150 | 6 | none | fast |
| `best-100-6` | otsu | 100 | 6 | none | tessdata_best |
| `best-100-11` | otsu | 100 | 11 | none | tessdata_best |

## Scoring

    python tools/bench/score.py --md tools/bench/results/summary.md

Per page and variant it reports:

- **token P/R/F1** - bag of words over lowercased `[a-z]+(?:['-][a-z]+)*`
  tokens of length >= 3,
- **uniqR** - share of distinct ground-truth words read at least once (this is
  what decides whether a word can ever reach the bar),
- **rareR** - same, restricted to the rare-word list,
- **merged%** - share of output tokens longer than 24 characters, the
  fingerprint of Tesseract merging whole lines when the space is missing from
  its whitelist,
- **ms** - median end-to-end milliseconds per page.

Results and the conclusions drawn from them live in
[`REPORT.md`](REPORT.md).

## Requirements

Python 3.11+ and:

    python -m venv tools/bench/.venv
    tools/bench/.venv/Scripts/python.exe -m pip install -r tools/bench/requirements.txt

Tesseract 5.x (`run_tesseract.py` looks in `D:\msys64\ucrt64\bin`), Microsoft
Edge for rendering, and Windows 10/11 for `run_winocr.ps1`.

`models/tessdata-best/eng.traineddata` is downloaded by hand from
<https://github.com/tesseract-ocr/tessdata_best>; it is not committed. The
`configs/` subdirectory next to it is copied from the installed tessdata so the
`tsv` output config resolves.
