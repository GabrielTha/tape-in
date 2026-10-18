# Tape In

Tape In is an open-source audio receiver for macOS (Windows later) that
works with AirPlay: pick it as the audio output on an iPhone, iPad or
another computer and the sound plays on this machine, or goes straight
into your DAW through a loopback device. Lossless, stereo, several sources
at once, controlled from the menu bar.

It is a clean-room reconstruction of the feature set of auxCord by
Mountainroad DSP. Roadmap: standalone macOS app (v1), AU/VST3 plugin (v2),
Windows port.

> AirPlay is a trademark of Apple Inc., registered in the U.S. and other
> countries and regions. Tape In is not affiliated with, endorsed by or
> sponsored by Apple Inc.

## Status

**Phase 3 built (awaiting the maintainer's acceptance tests):** the menu-bar
macOS app now shows what each sender is playing — track, artist, album and
album artwork — and can drive the sender back (play/pause, next, previous),
all in a redesigned vertical neumorphic UI. Phase 2 (the app: per-source
fader, mute, solo, renamable destination, buffer modes, start-at-login) is
accepted and merged; Phase 1 (`tapein-cli` receiver to WAV, verified
sample-exact against pyatv and cliraop) is merged to `main`. See
[docs/SPEC.md](docs/SPEC.md) for the plan,
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design, and the phase
reports in [docs/phase-reports/](docs/phase-reports/) for what was built and
how to test it.

```sh
cmake -S . -B build -DTAPEIN_BUILD_APP=ON
cmake --build build --parallel --target TapeIn
open "build/app/TapeIn_artefacts/Debug/Tape In.app"   # allow Local Network when asked
```

## Building

Requirements: CMake ≥ 3.25, a C++20 compiler (Apple clang 15+, GCC 12+,
MSVC 2022), Git. Dependencies are fetched at configure time.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Universal macOS build:

```sh
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
```

Sanitizers (the test suite must stay clean under them):

```sh
cmake -S . -B build-asan -DTAPEIN_SANITIZE=address,undefined
```

## License

AGPL-3.0-or-later. See [LICENSE](LICENSE). Third-party components and their
licenses are listed in [THIRD_PARTY.md](THIRD_PARTY.md).
