<p align="center">
  <img src="app/Resources/images/tapein_logo.png" alt="Tape In" width="420">
</p>

<p align="center">
  <a href="https://github.com/GabrielTha/tape-in/actions/workflows/ci.yml"><img src="https://github.com/GabrielTha/tape-in/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <img src="https://img.shields.io/badge/version-0.1.0-e0a35c" alt="Version 0.1.0">
  <img src="https://img.shields.io/badge/platform-macOS%2013%2B-111" alt="macOS 13+">
  <img src="https://img.shields.io/badge/license-AGPL--3.0--or--later-blue" alt="AGPL-3.0-or-later">
</p>

**Tape In** is an open-source audio receiver for macOS that works with
AirPlay: pick it as the audio output on an iPhone, iPad or another computer
and the sound plays on this machine — or goes straight into your DAW through
a loopback device. Lossless, stereo, several sources at once, controlled
from a single window.

> AirPlay is a trademark of Apple Inc., registered in the U.S. and other
> countries and regions. Tape In is not affiliated with, endorsed by or
> sponsored by Apple Inc.

## Features

- **AirPlay-1 (RAOP) receiver** — lossless 44.1 kHz/16-bit stereo (ALAC or PCM)
- **Multiple senders at once**, mixed into one output, each with its own
  fader, meters, mute and solo
- **Now playing** — track, artist, album and artwork from each sender
- **Remote transport** — play/pause, next, previous sent back to the sender
- **Sampling** — record the clean incoming mix and drag the WAV straight
  into Ableton, Finder or any other app
- **Any output device** — including loopback drivers (BlackHole, Focusrite
  Loopback…) to route into a DAW
- **Buffer modes** — Sync, Short or Monitor latency profiles
- **Access control** — optional password, or approve each new sender by hand

## Installing

There is no signed binary yet (that is the next milestone), so for now you
build it from source — about two minutes on an Apple Silicon machine:

```sh
git clone https://github.com/GabrielTha/tape-in.git
cd tape-in
cmake -S . -B build -DTAPEIN_BUILD_APP=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel --target TapeIn
open "build/app/TapeIn_artefacts/Release/Tape In.app"
```

macOS will ask for the **Local Network** permission the first time — allow
it, senders cannot find Tape In without it. If the app does not show up on
your iPhone, check that both devices are on the same network and that your
router does not isolate wireless clients ("AP/client isolation").

### Recording into a DAW

Install a loopback audio driver such as
[BlackHole](https://github.com/ExistentialAudio/BlackHole), select it as
Tape In's output device, and pick the same driver as the input of a track
in your DAW. Or skip the DAW routing entirely: hit the record button in
Tape In and drag the sample out of the window.

## Building for development

Requirements: macOS 13+, CMake ≥ 3.25, Apple clang 15+ (Command Line Tools
are enough), Git. Dependencies are fetched at configure time.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Universal binary:

```sh
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
```

Sanitizers (the test suite must stay clean under them):

```sh
cmake -S . -B build-asan -DTAPEIN_SANITIZE=address,undefined
```

The core library (`core/`) is pure C++20 with no JUCE dependency and also
builds on Linux, where CI runs the fuzzing harnesses. See
[docs/SPEC.md](docs/SPEC.md) for the roadmap and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design.

## Roadmap

- **v1 — macOS app** *(you are here)*: signed and notarized builds are the
  next milestone.
- **v2 — AU/VST3 plugins**: receive AirPlay audio directly on a DAW track.
- **Windows port**: after v1 is stable.

## Versioning

Tape In follows [Semantic Versioning](https://semver.org). Releases are
tagged `vMAJOR.MINOR.PATCH`; everything before 1.0 is a preview and minor
versions may change behaviour. The current version lives in the top-level
`CMakeLists.txt` (`project(TapeIn VERSION …)`).

## License

[AGPL-3.0-or-later](LICENSE). Third-party components and their licenses
are listed in [THIRD_PARTY.md](THIRD_PARTY.md). Tape In is a clean-room
reconstruction of the feature set of auxCord by Mountainroad DSP, with no
access to its code.
