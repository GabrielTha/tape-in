#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Tape In contributors
"""Generates the deterministic test signal used by the integration tests.

44.1 kHz, 16-bit, stereo, 20 s by default:
  - a 50 ms full-scale click at t = 1.0 s on both channels (alignment marker)
  - 1 kHz (left) / 1.5 kHz (right) tone bursts of 250 ms every 500 ms
  - a slow ramp on the right channel so every packet is distinguishable
  - silence elsewhere
"""

import argparse
import math
import struct
import wave

RATE = 44100


def sample(t: float, ch: int) -> int:
    # Click marker.
    if 1.0 <= t < 1.05:
        return 32000 if int(t * 2000) % 2 == 0 else -32000
    # Tone bursts.
    phase_in_second = t % 0.5
    if phase_in_second < 0.25:
        freq = 1000.0 if ch == 0 else 1500.0
        env = min(1.0, phase_in_second / 0.005, (0.25 - phase_in_second) / 0.005)  # 5 ms fades
        v = 12000.0 * env * math.sin(2 * math.pi * freq * t)
    else:
        v = 0.0
    if ch == 1:
        v += 2000.0 * ((t % 4.0) / 4.0)  # slow ramp
    return max(-32768, min(32767, int(round(v))))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("output")
    ap.add_argument("--seconds", type=float, default=20.0)
    args = ap.parse_args()
    frames = int(args.seconds * RATE)
    data = bytearray()
    for i in range(frames):
        t = i / RATE
        data += struct.pack("<hh", sample(t, 0), sample(t, 1))
    with wave.open(args.output, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(data))
    print(f"wrote {args.output}: {frames} frames ({args.seconds} s)")


if __name__ == "__main__":
    main()
