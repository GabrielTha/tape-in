# Phase 2 report — standalone macOS app (JUCE)

Date: 2026-10-16. Branch: `phase-2/app`. Plan:
[`docs/superpowers/plans/2026-10-16-phase-2-app.md`](../superpowers/plans/2026-10-16-phase-2-app.md).

## What was built

A menu-bar macOS app, **Tape In**, that advertises one AirPlay-1 receiver,
plays every sender through the output device you choose, and gives each
source a fader, mute, solo and a level meter. It can be renamed and can
start at login.

The Phase-1 core grew the whole audio-side pipeline (pure C++20, no JUCE):

| Module | What it does |
|---|---|
| `buffer/SpscRing` | lock-free single-producer/single-consumer PCM ring, pointer-free header (ready for the v2 plugin's shared memory) |
| `dsp/Resampler` | libsamplerate sinc wrapper, variable ratio, no allocation in `process()` |
| `dsp/DriftController` | slow PI loop on the ring fill level; steers the ratio within ±0.2 % |
| `dsp/SourceStream` | one session's audio-thread consumer: ring → resampler → drift → gain/mute/solo → meters, with fades on start, underrun, flush and stop |
| `dsp/Mixer` | sums the sources with lock-free slots, solo gating and a soft clip |
| `session/Engine` | owns the receiver, the mixer and the source registry keyed by DACP-ID; a control API and an event queue for the UI |
| `platform/ILoginItem` | null + macOS `SMAppService` start-at-login |

The JUCE app (`app/`) is a thin layer: `Settings` (persisted name, hardware
id, buffer mode, device state, window bounds), `AudioOutput` (Core Audio
callback → `Mixer::render`, never changes the device rate), `AppEngine`
(wires it together), `MainComponent` (header, source rows, meters, faders,
Advanced disclosure), `TrayIcon` and the About box.

## Verification

| Check | Result |
|---|---|
| Unit tests (Catch2) | 181 cases, green on Debug, ASan+UBSan and TSan (macOS, Apple clang) |
| SpscRing | single-thread + a 1 M-frame two-thread stress under TSan |
| Resampler / DriftController | ratio sweeps, a settling test against simulated sender drift |
| SourceStream | start/underrun/flush/stop fades, 44.1→48/96 k, mute, solo, sender-gain, a medium/best-quality render test |
| Mixer | summing, solo, mono fold-down, oversize and one-frame blocks, soft clip, lock-free garbage collection |
| Engine | a session becomes a source, audio reaches the mixer, volume, teardown+linger, DACP-ID reconnect keeps the fader, name clamp, buffer-mode target, a **real-time no-dropout** render test |
| App smoke test | streaming pyatv to the running app over loopback reaches `playing` at full level; the audio is continuous (stable ring fill, zero underruns); rename, output selection, mute/solo work; the app has no Dock icon, lives in the menu bar, and quits cleanly, unpublishing the receiver |
| Fuzzing (Linux clang CI) | an extra UBSan finding in Apple's ALAC decoder (a 32-bit shift on a zero-length suffix) was fixed and seeded into the regression corpus |

## How to test (maintainer)

```sh
cd /Users/gabriel/Documents/Dev/TapeIn
cmake -S . -B build -DTAPEIN_BUILD_APP=ON
cmake --build build --parallel --target TapeIn
open "build/app/TapeIn_artefacts/Debug/Tape In.app"
```

A log is written to `~/Library/Logs/Tape In/Tape In.log` (one status line
every two seconds while a source plays), useful if anything misbehaves.

1. **Local Network permission (first launch).** macOS will ask to let Tape In
   find devices on the local network. **Allow it** — without it the iPhone
   cannot see the receiver. If you miss the prompt: System Settings > Privacy
   & Security > Local Network > enable Tape In. (A terminal tool is exempt
   from this; a bundled app is not. An unsigned debug build like this one
   loses the grant on each rebuild, so you may be asked again; signing in
   Phase 4 fixes that.)
2. **Control Center.** On the iPhone, Control Center > audio card > AirPlay;
   pick "Tape In". Play anything. A row appears in the window with the phone's
   name, a moving level meter and "Playing". You should hear it on the Mac's
   selected output.
3. **Output device.** Change the output in the dropdown (built-in, your
   interface, or BlackHole) while a source plays. The audio follows without
   dropping the session; the rate label updates.
4. **Mixer.** Move the fader, Mute, Solo. With a second iPhone playing, both
   rows show; Solo on one silences the other.
5. **Rename.** Type a new name and press Return. Within a couple of seconds
   `dns-sd -B _raop._tcp local.` shows the new name and the iPhone list
   updates (you may need to reopen Control Center).
6. **Start at login.** Toggle it; if macOS says it needs approval, the hint
   points you to System Settings > General > Login Items.
7. **Advanced.** Open Advanced: switch the buffer mode (Sender sync ≈ 2 s is
   the default; Short and Monitoring trade latency for robustness) and try
   "Ignore sender volume" (unity gain into a DAW regardless of the phone's
   volume).
8. **Robustness.** Leave it an hour; sleep/wake the Mac; switch Wi-Fi network.
   The app must not crash; sources reconnect.
9. **Into a DAW.** Set the output to BlackHole (or the Focusrite loopback) and
   record it in Ableton Live.

## Known problems and limits

- **Local Network permission** gates visibility on macOS 15+/26, as above.
  This is the one thing to get right for the iPhone to see the receiver.
- **No code signing yet.** This is an unsigned debug/release build; Gatekeeper
  will warn, and the Local Network grant resets on rebuild. Signing and
  notarization are Phase 4 (they need your Apple Developer account).
- **The level meter** is driven from the audio thread and sampled by the UI;
  it decays smoothly. The 2 s text log samples the instantaneous peak, so an
  occasional `level 0.00` line there is normal and does not mean a dropout
  (watch `dropouts` instead).
- **Sender clock** drives the buffer target; the drift controller holds the
  ring level. Sender-sync keeps auxCord-like ~2 s latency by default.
- **Windows** builds the core in CI only; the app is macOS-only in this phase.
- **CI:** macOS runs core tests under ASan/UBSan plus the pyatv integration
  and now also builds the universal app as an artifact; Linux runs
  ASan/UBSan/TSan and 60 s of libFuzzer per parser; Windows builds and tests
  the core (one pre-existing ALAC decoder issue under Windows ASan is tracked
  for later and does not affect macOS).

## Decisions taken during the phase

The ledger has every ruling. The ones that matter beyond this phase: the
bundled app needs the macOS Local Network approval to advertise (proven by
the Phase-1 CLI advertising where the app does not, same core code); live
source stats in the UI come from `Receiver::sessionStats`; the resampler
quality defaults to medium (`SRC_SINC_MEDIUM_QUALITY`); and the intermittent
`level 0.00` in the status log is a meter-sampling artifact, not a dropout
(the real-time render test confirms the pipeline emits no silent blocks).
