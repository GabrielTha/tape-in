# Phase 1 report — minimal receiver (CLI)

Date: 2026-10-16. Branch: `phase-1/cli-receiver`. Plan:
[`docs/superpowers/plans/2026-10-16-phase-1-cli-receiver.md`](../superpowers/plans/2026-10-16-phase-1-cli-receiver.md).

## What was built

`tapein-cli` advertises one AirPlay-1 (RAOP) receiver on the LAN, accepts
any number of senders and writes each session's decoded 44.1 kHz / 16-bit /
stereo audio to its own WAV file. No audio device, no resampling, no mixer
yet: those are Phase 2.

Under it, `tapein_core` (pure C++20, no JUCE):

| Module | What it does |
|---|---|
| `rtsp/` | incremental RTSP request parser with hard limits (16 KiB headers, 64 headers, 4 MiB body), response serializer, SDP (ANNOUNCE) parser |
| `crypto/` | Mbed TLS 3.6.7: Apple-Challenge raw RSA signature, RSA-OAEP-SHA1 key unwrap, AES-128-CBC per-packet decryption; the RAOP key lives in one file (see `docs/LEGAL.md`) |
| `codec/` | Apple's reference ALAC decoder (Apache-2.0) behind a bounds-checked wrapper, hardened after fuzzing (see below) |
| `rtp/` | audio / resend-reply / sync / timing packet parsers with minimum-length checks, resend and timing request builders |
| `buffer/` | 512-slot sequence-indexed `JitterBuffer` with hold-back release, gap detection and resend scheduling; `WavWriter` |
| `net/` | non-blocking BSD/Winsock sockets (the only platform-conditional file in the core) and a `poll()` reactor with timers and a thread-safe `post()` |
| `session/` | `Receiver` (RTSP listener on IPv4+IPv6, protocol handlers, Bonjour record), `Session` (three UDP sockets, decrypt, decode, jitter buffer, timers), `IPcmSink` |

`tapein_platform` has the macOS `dns_sd` advertiser and a null advertiser for
CI. `tools/tapein-cli` adds argument parsing, a WAV sink and a raw-traffic
capture writer.

## Verification

| Check | Result |
|---|---|
| Unit tests (Catch2) | 126 cases (one opt-in mDNS case), green on Debug, ASan+UBSan and TSan builds (macOS, Apple clang 21) |
| Fuzz regression corpus | 4 harnesses, 168 seeds replayed under CTest in every build |
| Local mutation fuzzing | ≈ 1 000 000 mutated inputs under ASan+UBSan, 0 findings after hardening |
| pyatv end-to-end (clear PCM) | 12–15 s streamed over loopback, **0 differing samples** after marker alignment; in CTest and CI |
| cliraop end-to-end (clear PCM, encrypted PCM, encrypted ALAC) | all three **0 differing samples**; the full iTunes-style RSA + AES + ALAC path works |
| Bonjour | `dns-sd -B _raop._tcp` lists `<HWID>@<name>`; `dns-sd -L` shows the auxCord-identical TXT record on a dynamic port, next to the built-in macOS receiver |
| CI (`.github/workflows/ci.yml`) | macOS universal + ASan/UBSan + pyatv; Linux gcc/clang ASan/UBSan + TSan + pyatv + 60 s libFuzzer per harness; Windows MSVC ASan (core, platform null, tools, unit tests). **Not yet exercised: the repository has no remote.** |

### Fuzzing findings in Apple's ALAC decoder

Mutation fuzzing of the vendored reference decoder found four problems on
malformed packets; all are fixed, marked `// Tape In:` in the source and
listed in `third_party/alac/UPSTREAM`:

1. A partial-frame header could raise the sample count above the caller's
   buffer: **heap overflow write**, reachable from the network. Now bounded.
2. `mixBits` from the packet used as a shift count (UB, exponent up to 255).
   Now rejected when ≥ 32.
3. `denShift == 0` made `unpc_block` compute `1 << -1`, and Apple's own
   decoder reaches that path legitimately in predictor mode 1. Guarded.
4. int32 overflow in the predictor sums. The decoder is compiled with
   `-fwrapv`, making wraparound defined.

The wrapper additionally decodes from a private copy whose tail is zeroed,
so the reference bit reader can never read past the datagram.

## How to test (maintainer)

```sh
cd /Users/gabriel/Documents/Dev/TapeIn
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure            # unit + fuzz regression
mkdir -p ~/Desktop/tapein-captures
./build/tools/tapein-cli --name "Tape In Dev" --out-dir ~/Desktop/tapein-captures
```

1. **Control Center.** On the iPhone, open Control Center → audio card →
   AirPlay. "Tape In Dev" must be listed. Pick it and play anything
   (Apple Music, Spotify, YouTube, Voice Memos). The terminal prints
   `session N started` with the client name; Ctrl-C after a while prints the
   file name. Open the WAV in any player.
2. **Ten minutes without dropouts.** Play a 10-minute track, then Ctrl-C.
   The summary line shows `packets`, `resends a/b`, `missing` and `decode
   errors`. Acceptance: `missing 0` and `decode errors 0`; a few resends are
   normal on Wi‑Fi. The WAV length should be within a packet of the play
   time.
3. **Two phones.** Pick "Tape In Dev" on a second iPhone while the first
   plays. Each gets its own `session` line and its own WAV.
4. **Verbose.** Add `--verbose` to see every RTSP request, the
   Apple-Challenge inputs and our response.
5. **Capture for the test corpus** (useful, not required): add
   `--capture ~/Desktop/tapein-captures/cap` and play 20 s of music; send
   me the `cap/` folder. The RTSP log contains no secrets beyond the session
   AES key wrapped with the public key.
6. **The `et=0` experiment** (one afternoon, see ARCHITECTURE §10). Run each
   of these and check whether the iPhone lists the receiver and what happens
   when you pick it:
   ```sh
   ./build/tools/tapein-cli --name "Tape In et0" --txt-et 0
   ./build/tools/tapein-cli --name "Tape In noresp" --no-apple-response --verbose
   ```
   Report: listed or not; if listed, whether audio plays; the last RTSP line
   in the log before it gives up.

Also useful while it runs:

```sh
dns-sd -B _raop._tcp                                      # the service
dns-sd -L "<HWID>@Tape In Dev" _raop._tcp local.          # TXT record and port
```

## Known problems and limits

- **No audio output.** Phase 1 writes WAV only; listening happens in Phase 2.
- **Sender clock not used for playout.** Timing requests are sent and
  replies counted, but Phase 1 writes frames as they are released; the
  drift controller comes with the audio device in Phase 2.
- **L16 last packet.** pyatv/cliraop send a short final packet; it is
  zero-padded to 352 frames, so a received WAV can end with up to 7 ms of
  silence.
- **Short writes close the connection.** RTSP responses are written with one
  non-blocking `send`; a peer that cannot take 1 KiB is dropped rather than
  queued. Never observed.
- **Local Network permission.** From Terminal the CLI is exempt (Apple's
  rule for command-line tools). A bundled app will prompt; handled in
  Phase 2.
- **Windows:** the core, null advertiser and CLI build in CI; there is no
  DNS-SD backend yet, so `tapein-cli` on Windows needs `--port` and a sender
  pointed at it manually.
- **libFuzzer** does not run locally (Apple clang ships no runtime); the
  corpus replay runs everywhere, real fuzzing only on the Linux clang CI job.
- **CI has never run** because the repository has no remote yet.

## Decisions taken during the phase

The plan's ledger has every ruling. The ones that matter beyond this phase:
sessions are keyed by DACP-ID and a client announcing again replaces its own
session; `GET`/`POST` (AirPlay-2 probes) answer 404 so senders fall back to
the classic path; the capture taps and the session stats in
`IPcmSink::onSessionEnded` are part of the core API; libraop is MIT (the
Phase-0 note saying otherwise was wrong) but still not vendored.
