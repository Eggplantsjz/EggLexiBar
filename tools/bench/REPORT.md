# OCR kernel: what the measurements say

This report answers one question: **which OCR setup should WordLens use to read
English text off a 4K Windows screen every 1.5 seconds?**

Corpus, method and per-run raw data live in [`README.md`](README.md) and
`results/`. All numbers come from `score.py` over those result files, measured
on an i5-1155G7 (4 cores / 8 threads) with a 3840x2160 desktop at 150% scaling -
the setup the target user runs.

## TL;DR

| decision | before | after | why |
|---|---|---|---|
| `tessedit_char_whitelist` | letters only | **removed** | it deleted every space, so Tesseract returned one giant token per line: F1 **8%** -> **99%** |
| preprocessing | Otsu binarise | **grayscale + invert only** | binarising anti-aliased screen text costs ~1.4 F1 points and 0.3 s |
| upscale before OCR | 150% | **100%** | equal accuracy, ~0.6 s faster, no 24 MP intermediate image |
| page segmentation | psm 6 | **psm 6 (unchanged)** | psm 6/11/3 differ by <0.4 F1; psm 6 is the fastest |
| trained model | `tessdata_fast` | **`tessdata_fast` (unchanged)** | `tessdata_best` scored *worse* and took 2.3x longer |
| band-parallel OCR | - | **rejected** | 1.3x wall-clock for 3x CPU and a 0.3 F1 loss |
| RapidOCR / PP-OCR | - | **rejected** | same accuracy, 5x slower, +onnxruntime and 15 MB models |
| `Windows.Media.Ocr` | - | **not deployable yet** | fastest by 3x, but only 80% F1 without the en-US recogniser |

Result: F1 8.1% -> 99.3% and ~2.7 s -> ~2.1 s per 4K frame.

## 1. The whitelist was destroying the output

The old kernel passed

    -c tessedit_char_whitelist=ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'

A whitelist without a space does not just suppress punctuation: with the LSTM
engine the space becomes an unknown character, so it is dropped and every run of
words on a line is concatenated. One article page came back as

    FIELDNOTES  NotesonDistributedSystems  AdaLin  iZninreaduodatedyesteday
    EverydistributedsystemisanexerciseinacceptinguncertaintyThenetworkdrops

i.e. 31 "words" for a page that contains 246, none of which exists in the
dictionary. This is the main reason the bar showed almost nothing (or nonsense)
while the OCR itself looked busy.

Adding the space to the whitelist repairs the segmentation but still hurts:
F1 79.8%, and code/terminal pages collapse to 56-70% because digits and
punctuation are suppressed as well ("Ln 30, Col 17" loses its numbers). Dropping
the whitelist entirely is both simpler and better.

| variant | article | code | ui | doc | terminal | dense | F1 |
|---|---|---|---|---|---|---|---|
| `wl-100-11` (letters only, old) | 1.4 | 7.5 | 35.2 | 8.5 | 7.8 | 13.1 | **8.1** |
| `wls-100-11` (letters + space) | 97.8 | 56.2 | 96.5 | 89.7 | 69.9 | 60.1 | 79.8 |
| `no-wl-100-11` (none) | 98.2 | 98.7 | 96.5 | 99.5 | 97.0 | 95.8 | **97.6** |

## 2. Binarising hurt; plain grayscale won

The kernel converted the frame to grayscale, inverted dark themes and then
applied an Otsu threshold. That last step throws away the anti-aliasing ramp
that the LSTM engine uses to separate `rn` from `m`, `l` from `I`, or `0` from
`O` in small UI fonts.

| variant | scale | prep | article | code | ui | doc | terminal | dense | F1 | ms |
|---|---|---|---|---|---|---|---|---|---|---|
| `no-wl-100-6` | 100 | otsu | 98.4 | 98.7 | 96.5 | 99.3 | 97.5 | 93.1 | 97.9 | 2029 |
| `gray-100-6` | 100 | gray | 99.4 | 99.6 | 94.8 | 99.3 | 99.3 | 97.0 | **99.3** | 2128 |
| `gray-150-6` | 150 | gray | 99.4 | 98.7 | 95.9 | 98.8 | 99.3 | 96.1 | 98.7 | 2332 |
| `gray-075-6` | 75 | gray | 99.4 | 97.9 | 95.9 | 99.8 | 99.8 | 89.6 | 98.6 | 1600 |
| `gray-150-11` | 150 | gray | 99.4 | 97.9 | 96.5 | 99.5 | 99.8 | 98.9 | 99.1 | 3410 |

Binarisation is not catastrophic, but it is strictly worse than leaving the
grayscale alone, and it costs 150-350 ms of preprocessing per frame on 4K.

Upscaling to 150% was in the kernel because "Tesseract likes 30 px tall text".
On a 4K screen at 150% scaling the text is already 26-33 px tall, so the extra
pass buys nothing and doubles the pixels Tesseract has to walk.

75% is the interesting speed point (1.6 s, -25%) but it is the one variant that
falls off on the `dense` page (89.6% vs 97.0%) - exactly the small-text case the
app is supposed to help with. Not worth the trade.

> Note: the settings still expose the scale factor, so a user on a 1080p screen
> (where 12 px text really is too small for Tesseract) can set 150%.

## 3. psm and the trained model do not matter much

| variant | F1 | ms |
|---|---|---|
| `no-wl-100-3` | 97.6 | 1956 |
| `no-wl-100-11` | 97.6 | 1964 |
| `no-wl-100-6` | 97.9 | 2029 |
| `best-100-11` (`tessdata_best`) | 97.5 | 4785 |
| `best-100-6` (`tessdata_best`) | 97.2 | 4062 |

`tessdata_best` is 15 MB instead of 4 MB and 2.3x slower, and it is *not* more
accurate on screen text: it is trained on scanned documents, while screen text
is generated from the same font stack the fast model was distilled on. Keep the
fast model.

psm 3 (fully automatic), 6 (uniform block) and 11 (sparse) all land within
0.4 points. psm 6 is slightly better and slightly faster on this corpus, and it
is what the app already defaults to.

## 4. More CPU cores do not help Tesseract

Tesseract is single-threaded, so the obvious latency fix is to cut the frame
into horizontal bands at blank rows (no line is ever cut) and run one process
per band.

| variant | workers | F1 | wall ms | CPU ms |
|---|---|---|---|---|
| `w1-g6` | 1 | 99.3 | 2001 | 2100 |
| `w2-g6` | 2 | 99.3 | 2418 | 5500 |
| `w4-g6` | 4 | 98.2 | 2538 | 7800 |
| `w4-6` (binarised) | 4 | 97.5 | 1499 | 4100 |
| `w4-11` (binarised) | 4 | 97.1 | 1508 | 4700 |

On a 4-core/8-thread ultrabook the win is at most 1.3x, and on the best-quality
preprocessing it is *negative*: every process pays the ~0.4 s model load, and
the hyperthreads contend for the same execution units. Three times the CPU for a
few hundred milliseconds is a bad trade on battery, and it loses 0.3-1.1 F1
because each band re-estimates its own layout. Rejected.

## 5. The other engines

### RapidOCR (PP-OCR v3, onnxruntime)

Self-contained det + cls + rec pipeline, no external executable. 98.4% F1 - on
par with Tesseract - but **9.5 s per 4K frame**. The detector is the reason: the
default `limit_type: min` only upscales, so DBNet sees the full 3840x2160.
Capping the long side at 736/1280/1920 changes the total by less than 20%
because recognition then dominates. Even at 50% input it needs ~7 s.

Costs: onnxruntime + opencv + 15 MB of models, and a C++ integration that would
have to ship all of that. Rejected for now; revisit only if a GPU path or a much
smaller English recogniser becomes available.

### Windows.Media.Ocr

The OCR engine built into Windows: in-process, ~0.7 s per 4K frame, no
dependency at all. That makes it by far the most attractive latency-wise.

The catch is the recogniser language. This machine only has `zh-Hans-CN`
installed; `TryCreateFromLanguage("en-US")` returns null. With the Chinese
recogniser the score is 79.8% F1 - 20 points below Tesseract - because it is
tuned for mixed Chinese/English text:

| page | article | code | ui | doc | terminal | dense | F1 | ms |
|---|---|---|---|---|---|---|---|---|
| win-zh-Hans-CN | 90.4 | 80.5 | 79.1 | 83.5 | 79.2 | 72.8 | 79.8 | 719 |

With the en-US pack installed it would very likely beat Tesseract on both axes,
and it is worth re-running `run_winocr.ps1` on a machine that has it. It is not
a safe default today (language packs are an optional Windows feature and adding
one needs administrator rights), and calling WinRT asynchronously from a
MinGW/Qt build is a substantial piece of plumbing. Recorded as the next
candidate, not shipped.

## 6. What the kernel does now

`src/TesseractOcr.cpp`:

1. grayscale, invert if the frame is dark, **no binarisation**,
2. optional upscale (default **100%**, no-op),
3. `--psm 6 --oem 1`, `tessedit_do_invert=0`, table analysis off,
4. **no `tessedit_char_whitelist`**,
5. `tessdata_fast`, one thread, TSV parsed into word boxes.

`src/Settings.h`: `ocrScalePercent` default 150 -> 100.

Measured on the benchmark corpus: **F1 99.3% (was 8.1%), 2.1 s median per 4K
frame (was ~2.7 s)**, and the per-page spread is now 0.9-3.0 s instead of
"whatever the merged-line bug produced".

## 7. Where the remaining latency is

Two seconds per scan is still longer than the 1.5 s scan interval, so a full
screen scan saturates one core continuously. The next steps, in order of value:

1. **Scan the active window, not the screen** (already a capture mode). Cost
   scales with area: a 1600x1000 window is ~4x cheaper than 3840x2160.
2. **OCR only the tiles that changed.** The controller already compares frames
   to skip static screens; keeping the diff as a dirty-region mask and OCR-ing
   only those tiles would make scrolling text cheap.
3. **Cache per-line results.** Identical line crops appear on almost every
   frame; a small hash -> text cache would remove most of the recognition work
   at the cost of a few MB.
4. Re-evaluate `Windows.Media.Ocr` behind en-US detection if a Windows-native
   backend ever becomes acceptable.
