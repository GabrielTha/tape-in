#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Tape In contributors
"""Compares a received WAV against the file that was sent.

Aligns the two by the first sample whose absolute value exceeds a threshold
(the click marker at t = 1 s in make_test_wav.py), then compares sample by
sample over the overlapping region. Exit code 0 iff no sample differs (after
alignment) and at least --min-seconds of audio overlapped; otherwise 1 with a
short report.
"""

import argparse
import struct
import sys
import wave


def read_wav(path: str):
    with wave.open(path, "rb") as w:
        if w.getnchannels() != 2 or w.getsampwidth() != 2 or w.getframerate() != 44100:
            raise SystemExit(f"{path}: expected 44100 Hz / 16-bit / stereo, got "
                             f"{w.getframerate()} Hz / {w.getsampwidth() * 8}-bit / {w.getnchannels()} ch")
        raw = w.readframes(w.getnframes())
    count = len(raw) // 2
    return struct.unpack(f"<{count}h", raw[: count * 2])


def first_loud(samples, threshold: int) -> int:
    for i, s in enumerate(samples):
        if abs(s) >= threshold:
            return i
    return -1


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("sent")
    ap.add_argument("received")
    ap.add_argument("--threshold", type=int, default=20000)
    ap.add_argument("--min-seconds", type=float, default=5.0)
    ap.add_argument("--max-diff-samples", type=int, default=0)
    args = ap.parse_args()

    sent = read_wav(args.sent)
    recv = read_wav(args.received)
    a = first_loud(sent, args.threshold)
    b = first_loud(recv, args.threshold)
    if a < 0 or b < 0:
        print(f"alignment marker not found (sent idx {a}, received idx {b})")
        return 1
    # Align to a frame boundary (interleaved stereo: even index).
    a -= a % 2
    b -= b % 2
    overlap = min(len(sent) - a, len(recv) - b)
    seconds = overlap / 2 / 44100
    print(f"received leads sent by {(b - a) / 2 / 44100:.3f} s; overlap {seconds:.2f} s")
    if seconds < args.min_seconds:
        print(f"too little overlap (< {args.min_seconds} s)")
        return 1

    diffs = 0
    max_abs = 0
    first_diff = -1
    histogram = {}
    for i in range(overlap):
        d = recv[b + i] - sent[a + i]
        if d != 0:
            diffs += 1
            if first_diff < 0:
                first_diff = i
            max_abs = max(max_abs, abs(d))
            bucket = min(abs(d), 1000) // 100 * 100
            histogram[bucket] = histogram.get(bucket, 0) + 1
    print(f"differing samples: {diffs} of {overlap} (max |diff| {max_abs})")
    if diffs:
        print(f"first difference at {first_diff / 2 / 44100:.3f} s after the marker")
        for bucket in sorted(histogram):
            print(f"  |diff| in [{bucket}, {bucket + 100}): {histogram[bucket]}")
    return 0 if diffs <= args.max_diff_samples else 1


if __name__ == "__main__":
    sys.exit(main())
