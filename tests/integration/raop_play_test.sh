#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Tape In contributors
#
# Encrypted-path end-to-end test with philippe44/libraop's cliraop (MIT).
# cliraop speaks classic RAOP exactly like iTunes did: Apple-Challenge,
# RSA-OAEP-wrapped AES key, AES-128-CBC audio, PCM (default) or ALAC (-a).
#
# Usage: raop_play_test.sh <path to tapein-cli> <path to cliraop> [seconds]
#   cliraop: build it from https://github.com/philippe44/libraop or use one
#   of the prebuilt binaries in that repository's bin/ directory.
#
# Runs three sessions (clear PCM, encrypted PCM, encrypted ALAC) and compares
# each received WAV with what was sent. Exit code 0 iff all three match.

set -euo pipefail

CLI=${1:?tapein-cli path}
RAOP=${2:?cliraop path}
SECONDS_TO_SEND=${3:-10}
HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d -t tapein-raop)
trap 'rm -rf "$TMP"' EXIT

python3 -I "$HERE/make_test_wav.py" "$TMP/sent.wav" --seconds "$SECONDS_TO_SEND" > /dev/null
# cliraop reads raw 16-bit little-endian PCM; strip the 44-byte WAV header.
tail -c +45 "$TMP/sent.wav" > "$TMP/sent.pcm"

run_case() {
    local label=$1; shift
    local dir="$TMP/$label"
    mkdir -p "$dir/out"
    "$CLI" --name "Tape In $label" --out-dir "$dir/out" > "$dir/cli.out" 2> "$dir/cli.err" &
    local cli_pid=$!
    disown "$cli_pid"
    sleep 1
    local port
    port=$(grep '^port:' "$dir/cli.out" | cut -d' ' -f2)
    # -l latency in frames, -d 0 silent; cliraop keeps running after the file ends, so bound it.
    "$RAOP" "$@" -p "$port" -d 0 -l 22050 -v 100 127.0.0.1 "$TMP/sent.pcm" > "$dir/raop.out" 2>&1 &
    local raop_pid=$!
    disown "$raop_pid"
    sleep $((SECONDS_TO_SEND + 4))
    kill "$raop_pid" 2>/dev/null || true
    sleep 2
    kill -INT "$cli_pid" 2>/dev/null || true
    while kill -0 "$cli_pid" 2>/dev/null; do sleep 0.2; done
    echo "== $label: $(grep -o 'announced by.*' "$dir/cli.err" | head -1)"
    grep -o 'ended (.*' "$dir/cli.err" | head -1
    python3 -I "$HERE/wavcompare.py" "$TMP/sent.wav" "$dir"/out/*.wav --min-seconds $((SECONDS_TO_SEND - 3))
}

status=0
run_case clear-pcm       || status=1
run_case encrypted-pcm   -e || status=1
run_case encrypted-alac  -e -a || status=1
exit $status
