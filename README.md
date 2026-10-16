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

**Phase 1 done (awaiting the maintainer's acceptance tests):**
`tapein-cli` advertises a receiver, accepts any number of senders and writes
each session to a WAV file. Verified end to end against pyatv (clear PCM)
and cliraop (encrypted PCM and ALAC) with sample-exact results. No app, no
audio output yet; that is Phase 2. See [docs/SPEC.md](docs/SPEC.md) for the
plan, [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design and
[docs/phase-reports/phase-1.md](docs/phase-reports/phase-1.md) for what was
built and how to test it.

```sh
./build/tools/tapein-cli --name "Tape In" --out-dir ~/Desktop/captures
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
