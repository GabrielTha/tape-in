# Integration tests

These drive the real `tapein-cli` binary over loopback with an open-source
sender and compare what was received with what was sent.

## pyatv (unencrypted PCM path)

```sh
python3 -m venv build/venv
build/venv/bin/pip install -r tests/integration/requirements.txt
build/venv/bin/python tests/integration/pyatv_stream_test.py --cli build/tools/tapein-cli
```

What it covers: RTSP state machine, RTP audio/control/timing, hold-back
release, resend requests, WAV output, sample-exact comparison after
alignment on the click marker. What it does not cover: Apple-Challenge,
RSA/AES and ALAC, because pyatv sends `L16` without encryption. Those are
covered by unit tests with OpenSSL-generated vectors and by a real iPhone.

Wired into CTest as `pyatv_integration` when configured with
`-DTAPEIN_INTEGRATION=ON` (CI does this on macOS and Linux).

## raop_play (encrypted ALAC path, best effort)

See `raop_play_test.sh`. philippe44/libraop has no license file, so it is
built as a test tool only when `TAPEIN_LIBRAOP=1` and never vendored.

## Tools

- `make_test_wav.py` — deterministic 44.1 kHz/16-bit/stereo signal with a
  click marker at 1 s, tone bursts and a slow ramp.
- `wavcompare.py sent.wav received.wav` — aligns on the marker and reports
  differing samples; exit 0 only if none differ.
