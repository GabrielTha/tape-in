# CLAUDE.md — Tape In project conventions

Read this first in every session. It is the durable context for the project.
Scope and phases: `docs/SPEC.md`. Design and decisions: `docs/ARCHITECTURE.md`.

## What this is

Tape In is an open-source (AGPLv3) AirPlay-1 (RAOP) audio receiver. v1 is a
macOS menu-bar app (JUCE); v2 adds AU/VST3 plugins; a Windows port follows.
It reproduces the feature set of auxCord (Mountainroad DSP) without access to
its code: parity, not copying.

## Language

- Talk to the maintainer in **Portuguese (pt-BR)**.
- Code, comments, identifiers, commit messages, docs, issues: **English**.
- Do not use "AirPlay" in product names or bundle IDs. Descriptive use only
  ("works with AirPlay"), always with the no-affiliation notice.

## Workflow rules (from SPEC.md §6 and §8)

- Work phase by phase. At the end of a phase deliver: summary, exact test
  instructions, known problems. Then **stop and wait for the maintainer's OK**.
- One branch per phase (`phase-N/<topic>`), small descriptive commits.
- **Ask before**: adding a dependency whose license is not AGPLv3-compatible;
  anything that needs `sudo` or installs system components (including
  `brew install` of system-level tools); anything that needs the maintainer's
  Apple Developer account.
- If the spec is infeasible or a better path exists, say so and propose the
  alternative. Do not silently deviate.

## Repository layout (target; see ARCHITECTURE.md for the rationale)

```
core/        tapein-core: pure C++20, no JUCE. RTSP/RTP, crypto glue, jitter
             buffer, clock model, resampler, mixer, DACP. Unit-tested.
platform/    tapein-platform: per-OS implementations of core interfaces
             (mDNS, crypto backend, login item, sockets if needed).
app/         JUCE app "Tape In": UI, audio I/O, menu-bar icon. Consumes core.
tools/       CLI tools (Phase 1 receiver-to-WAV, test senders, fuzz harness).
tests/       unit + integration tests, test vectors, fuzz corpora.
third_party/ vendored deps, each with its LICENSE and a line in THIRD_PARTY.md.
docs/        SPEC.md, ARCHITECTURE.md, phase reports, decisions.
cmake/       toolchain and helper modules.
```

## Build and toolchain

- C++20, CMake ≥ 3.25, Ninja. macOS: Apple clang; Windows: MSVC and clang-cl.
- Universal binary: configure with `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.
- Minimum macOS: see ARCHITECTURE.md (decision: 13.0 deployment target).
- `core/` must build and pass tests **without JUCE** on macOS, Linux and
  Windows. Never include a JUCE header under `core/` or `platform/`.
- Sanitizer builds are first-class: `-DTAPEIN_SANITIZE=address,undefined`.
  CI runs ASan/UBSan on macOS and Linux, ASan on Windows (MSVC).
- Bundle ID prefix: `app.tapein` (`app.tapein.mac` for the app).

## Code conventions

- Files: `snake_case.cpp/.h` in core/platform; JUCE app may use `PascalCase`
  for component classes. Namespaces: `tapein::core`, `tapein::platform`,
  `tapein::app`.
- Types `PascalCase`, functions/variables `camelCase`, constants `kPascalCase`,
  members `m_` prefix only in core (JUCE style in app).
- Every source file starts with an SPDX header:
  `// SPDX-License-Identifier: AGPL-3.0-or-later` and
  `// Copyright (C) 2026 Tape In contributors`.
- Vendored or ported third-party code keeps its original header and license;
  list it in `THIRD_PARTY.md` with license and upstream commit.
- Platform code only behind interfaces declared in `core/include/tapein/`.
  Objective-C++ (`.mm`) lives only under `platform/macos/` and `app/`.
- No exceptions across the core API boundary; use `std::expected`-style
  result types. No RTTI requirements.
- No `using namespace` in headers. Prefer `std::span`, `std::string_view`.

## Audio-thread rules (non-negotiable)

- Zero locks, zero allocation, zero I/O, zero logging on the audio thread.
- Communication with the audio thread only via lock-free SPSC rings and
  atomics. Parameter changes are atomics read once per callback.
- Underrun: fade to silence over a few ms, never emit garbage or stale data.
- Output format change (device/sample-rate switch): rebuild the resampler
  off-thread, swap atomically.

## Network-input rules (non-negotiable)

- Every byte from the network is hostile. Explicit length checks before
  every field read; bounded header counts and sizes; `Content-Length` cap;
  reject unknown states (SETUP before ANNOUNCE etc.).
- Parsers are pure functions over `std::span<const uint8_t>` so they can be
  unit-tested and fuzzed (libFuzzer harness under `tests/fuzz/`).
- Fixed-size buffers for RTP (2048 bytes); drop oversize datagrams.

## Testing

- Unit: ring buffer, jitter buffer, clock model, resampler + drift control,
  RTSP/SDP/DMAP/RTP parsers (with malformed inputs), ALAC wrapper, crypto
  vectors (Apple-Challenge → Apple-Response, rsaaeskey unwrap).
- Integration: an open-source sender streams a known WAV; the received WAV
  is compared sample-by-sample (allowing for the leading silence offset).
- Manual matrix (maintainer): iPhone apps, another Mac, Windows sender; sinks
  BlackHole and Focusrite loopback into Ableton Live.
- Local checks that need no code: `dns-sd -B _raop._tcp` (service visible),
  `dns-sd -L "<id>@<name>" _raop._tcp local.` (TXT record and port).

## Facts observed on the maintainer's machine (2026-10-16)

- macOS 26.5, Apple clang 21, CMake 4.3, Command Line Tools only (no Xcode
  app installed; `xcodebuild` is unavailable, use Ninja + `codesign`).
- The built-in macOS AirPlay Receiver listens on TCP 5000 and 7000 and
  advertises `_raop._tcp` + `_airplay._tcp` on 7000. Third-party RAOP
  receivers on dynamic ports coexist with it.
- auxCord 2.6.1 advertises only `_raop._tcp` on a dynamic port with TXT
  `am=MntRd,1 ch=2 cn=0,1 da=true ek=1 et=0,1 md=0,1,2 pw=false sm=false
  sr=44100 ss=16 sv=false tp=UDP txtvers=1 vn=3 vs=130.14`. It is a JUCE app
  with OpenSSL statically linked, and groups RTSP connections into client
  sessions keyed by DACP-ID.

## Things learned while building (keep)

- Apple's reference ALAC decoder is not safe on hostile input as published:
  see `third_party/alac/UPSTREAM` for the four validation patches and the
  `-fwrapv` flag. Never update that vendored copy without re-applying them
  and re-running the fuzz corpus.
- pyatv (`atvremote --manual --protocol raop`) sends unencrypted L16 and
  only takes the classic path when `GET /info` is **not** 200. cliraop
  (philippe44/libraop, MIT, prebuilt binaries in its `bin/`) does the
  iTunes-style encrypted PCM/ALAC path; `tests/integration/raop_play_test.sh`.
- Apple clang has no libFuzzer runtime; `tests/fuzz` harnesses replay the
  corpus under CTest locally and fuzz for real on the Linux clang CI job.
- Gate sanitizer runs on the `ctest` exit code, never on grep of its output.
- Command-line tools launched from Terminal are exempt from the Local
  Network permission (Apple TN3179); a bundled app is not.

## Phase status

- Phase 0 (research and decisions): delivered 2026-10-16 on `phase-0/research`; decisions recorded in ARCHITECTURE.md §13.
- Phase 1 (CLI receiver): built 2026-10-16 on `phase-1/cli-receiver`; report in `docs/phase-reports/phase-1.md`; waiting for the maintainer's iPhone acceptance tests and OK.
- Phases 2–6: not started. See `docs/SPEC.md` §6.
