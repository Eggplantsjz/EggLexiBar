#!/usr/bin/env python3
"""Convert the ECDICT csv into the compact binary dictionary used by WordLens.

The output format (little endian) is:

    char   magic[8]      "WLDICT1\\0"
    uint32 count         number of entries
    uint32 blobOffset    always 16 + count * 12
    record[count]:       uint32 keyOffset, uint32 defOffset,
                         uint16 keyLength, uint16 defLength
    blob                 keys (lower case ASCII) and definitions ("phonetic\\nsense...")

Only single lower-case ASCII words that appear in words_alpha.txt are kept,
because those are the only strings the OCR pipeline can produce.
"""

import argparse
import csv
import os
import re
import struct
import sys

WORD_RE = re.compile(r"^[a-z]{2,24}$")


def parse_args():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.dirname(here)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", default=os.path.join(repo, ".cache", "ECDICT", "ecdict.csv"))
    parser.add_argument("--words", default=os.path.join(repo, "data", "words_alpha.txt"))
    parser.add_argument("--out", default=os.path.join(repo, "data", "dict.bin"))
    parser.add_argument("--max-senses", type=int, default=4)
    parser.add_argument("--max-chars", type=int, default=220)
    return parser.parse_args()


def clean_translation(text):
    text = text.replace("\\n", "\n")
    senses = []
    for raw in text.split("\n"):
        line = re.sub(r"\s+", " ", raw).strip()
        if not line:
            continue
        if line.startswith("[网络]") or line.startswith("[网络"):
            continue
        # Keep the domain marker ("[计] 属性"): it tells a specialised term
        # apart from the bare Chinese glosses of OCR fragments, and it is
        # useful on the card anyway.
        senses.append(line)
    return senses


def main():
    args = parse_args()
    if not os.path.exists(args.csv):
        sys.exit("csv not found: " + args.csv)

    with open(args.words, encoding="utf-8", errors="ignore") as handle:
        wanted = {line.strip().lower() for line in handle if line.strip()}

    entries = {}
    total = 0
    with open(args.csv, encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            total += 1
            word = (row.get("word") or "").strip().lower()
            if word not in wanted or not WORD_RE.match(word):
                continue

            senses = clean_translation((row.get("translation") or "").strip())
            if not senses:
                # Fall back to the English definition so the card is never empty.
                senses = clean_translation((row.get("definition") or "").strip())
            if not senses:
                continue

            phonetic = re.sub(r"\s+", " ", (row.get("phonetic") or "").strip())
            phonetic = phonetic.strip("/")

            if word in entries:
                old = entries[word]
                # Prefer the entry that actually has a Chinese translation.
                if old[0] and not phonetic:
                    continue

            entries[word] = (phonetic, senses)

    keys = sorted(entries)
    blob = bytearray()
    records = []
    max_def = 0
    for word in keys:
        phonetic, senses = entries[word]
        text = "\n".join(senses[: args.max_senses])
        if len(text) > args.max_chars:
            text = text[: args.max_chars].rstrip() + "…"
        definition = (phonetic + "\n" + text).encode("utf-8")
        key = word.encode("ascii")
        if len(definition) > 65000 or len(key) > 65535:
            continue
        max_def = max(max_def, len(definition))

        key_offset = len(blob)
        blob += key
        def_offset = len(blob)
        blob += definition
        records.append((key_offset, def_offset, len(key), len(definition)))

    blob_offset = 16 + len(records) * 12
    with open(args.out, "wb") as handle:
        handle.write(b"WLDICT1\0")
        handle.write(struct.pack("<II", len(records), blob_offset))
        for key_offset, def_offset, key_len, def_len in records:
            handle.write(struct.pack("<IIHH", key_offset, def_offset, key_len, def_len))
        handle.write(blob)

    size = os.path.getsize(args.out)
    print(f"rows read      : {total}")
    print(f"entries written: {len(records)}")
    print(f"largest entry  : {max_def} bytes")
    print(f"output         : {args.out} ({size / 1024 / 1024:.1f} MB)")

    for probe in ("idempotency", "copilot", "debug", "introduces", "ran"):
        hit = entries.get(probe)
        print(f"  {probe:<12} {'missing' if not hit else hit[1][0][:70]}")


if __name__ == "__main__":
    main()
