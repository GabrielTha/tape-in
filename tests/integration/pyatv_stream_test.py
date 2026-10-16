#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Tape In contributors
"""End-to-end test: pyatv streams a known WAV to tapein-cli over loopback.

Requires `atvremote` (pip install pyatv) on PATH or given with --atvremote.
pyatv speaks unencrypted RAOP with L16 payloads, so this exercises
discovery-less connection, the RTSP state machine, RTP, timing/control,
resends and the WAV path; the RSA/AES/ALAC path is covered by unit tests
and by the raop_play script.

Usage: pyatv_stream_test.py --cli build/tools/tapein-cli [--seconds 20]
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", required=True, help="path to tapein-cli")
    ap.add_argument("--atvremote", default=shutil.which("atvremote"))
    ap.add_argument("--seconds", type=float, default=20.0)
    ap.add_argument("--keep", action="store_true", help="keep the temp directory")
    args = ap.parse_args()
    if not args.atvremote:
        print("atvremote not found; pip install pyatv")
        return 2

    tmp = Path(tempfile.mkdtemp(prefix="tapein-it-"))
    sent = tmp / "sent.wav"
    out_dir = tmp / "out"
    out_dir.mkdir()
    subprocess.check_call([sys.executable, "-I", str(HERE / "make_test_wav.py"), str(sent), "--seconds", str(args.seconds)])

    hwid = "02AABBCCDD01"
    cli = subprocess.Popen(
        [args.cli, "--name", "Tape In IT", "--hwid", hwid, "--out-dir", str(out_dir), "--port", "0", "--verbose"],
        stdout=subprocess.PIPE, stderr=open(tmp / "cli.err", "w"), text=True)
    port = None
    deadline = time.time() + 10
    while time.time() < deadline and port is None:
        line = cli.stdout.readline()
        if not line:
            break
        m = re.match(r"port: (\d+)", line)
        if m:
            port = int(m.group(1))
    if port is None:
        cli.terminate()
        print("tapein-cli did not report its port")
        return 1
    print(f"tapein-cli listening on {port}")

    # pyatv: manual target (no discovery), RAOP protocol, stream the file.
    cmd = [args.atvremote, "--manual", "--address", "127.0.0.1", "--port", str(port), "--protocol", "raop",
           "--id", hwid, f"stream_file={sent}"]
    print(" ".join(cmd))
    t0 = time.time()
    rc = subprocess.call(cmd, stdout=open(tmp / "atvremote.out", "w"), stderr=subprocess.STDOUT, timeout=args.seconds + 60)
    print(f"atvremote exited {rc} after {time.time() - t0:.1f} s")

    time.sleep(3)  # let the receiver flush hold-back and close the session
    cli.send_signal(2)  # SIGINT
    try:
        cli.wait(timeout=10)
    except subprocess.TimeoutExpired:
        cli.kill()

    files = sorted(out_dir.glob("*.wav"))
    print("received files:", [f.name for f in files])
    print((tmp / "cli.err").read_text()[-2000:])
    if rc != 0:
        print((tmp / "atvremote.out").read_text()[-2000:])
        return 1
    if len(files) != 1:
        print("expected exactly one received WAV")
        return 1
    result = subprocess.call([sys.executable, "-I", str(HERE / "wavcompare.py"), str(sent), str(files[0]),
                              "--min-seconds", str(max(1.0, args.seconds - 3))])
    if not args.keep:
        shutil.rmtree(tmp, ignore_errors=True)
    else:
        print(f"kept {tmp}")
    return result


if __name__ == "__main__":
    sys.exit(main())
