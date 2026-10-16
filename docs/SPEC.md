# Tape In — Project Specification

> English rendering of the original project brief (Portuguese, 2026-10-16).
> This file is the source of truth for scope and phases. Architecture
> decisions live in [ARCHITECTURE.md](ARCHITECTURE.md); project conventions
> live in [../CLAUDE.md](../CLAUDE.md).

## Mission

**Tape In** is an open-source app that receives audio over Wi‑Fi (AirPlay)
from any device — iPhone, iPad, another Mac, a PC with an AirPlay sender, or
the output of a DAW on another computer — and plays it on this computer.

It is an open-source reconstruction of **auxCord** by Mountainroad DSP
(formerly *AlpHorn*). We have no access to its code. The goal is feature
parity, not a copy.

The plan has three stages:

- **v1**: standalone macOS app.
- **v2**: AU/VST3 plugin that brings the audio straight into a DAW track.
- **Later**: Windows version.

The code must be portable from the first commit, so that Windows and the
plugin can be added without rewriting the core.

Language: the maintainer is addressed in Portuguese. Code, comments,
identifiers, commits and documentation are in English, because the project
will be public.

---

## 1. v1 scope: standalone macOS app

1. **AirPlay destination on the network.** The Mac shows up as an AirPlay
   destination ("Tape In", or whatever name the user chooses). On an iPhone,
   the user picks it in Control Center and the audio plays on the Mac. Nothing
   has to be installed on the sending device.
2. **Configurable audio output.** The received audio goes to the output the
   user chooses: speakers, headphones, an audio interface, or a virtual /
   loopback device (BlackHole, the interface's own loopback) to reach a DAW.
3. **Editable name.** The destination name can be changed and is persisted.
4. **Many sources on one destination.** auxCord advertises up to 100
   simultaneous sources. Each source appears in a list with the device name,
   level meter, volume, mute and solo.
5. **Remote control of the source** from the app: play/pause, next, previous
   and source volume.
6. **Lossless audio**: ALAC 44.1 kHz / 16‑bit, which is what AirPlay delivers,
   in stereo.
7. **Robustness.** No clicks, hangs or crashes when:
   - the network drops;
   - the source disconnects;
   - the Mac sleeps and wakes;
   - the audio output is switched, unplugged, or changes sample rate.
8. **Menu bar.** The app lives in the menu bar (system tray on Windows) and
   offers "start at login".

## 2. Out of scope for v1 (tracked in the roadmap)

- **v2: AU and VST3 plugin.** Each instance becomes its own AirPlay
  destination, with state saved in the DAW project.
- **Windows port.** Standalone app and VST3 plugin, with WASAPI and ASIO.
- Several simultaneous destinations in the standalone app (the engine must
  already support it; only the UI is deferred).
- AirPlay 2.
- AAX / Pro Tools.
- AUv3.
- Our own virtual audio device.
- Multichannel audio.
- Sender for Android or browser.
- Low-latency mode.

## 3. Stack and constraints

- **Language and build**: C++20 with CMake. No Swift.
- **Framework**: JUCE 8 for the app layer, covering:
  - UI;
  - audio I/O (Core Audio now; WASAPI and ASIO on Windows later);
  - menu bar / tray icon;
  - packaging;
  - in v2, the plugin targets, in the same project.
- **License: AGPLv3.** JUCE is dual-licensed (AGPLv3 or commercial); we use
  the AGPLv3 option. SPDX headers and third-party notices from the start.
- **Protocol core in pure C++, with no JUCE dependency.** It covers RTSP/RTP,
  jitter buffer, mixer and resampler, and must be testable in isolation and
  reusable.
- **Platform-specific code only behind interfaces:**

  | Area | macOS | Windows |
  |---|---|---|
  | mDNS/Bonjour | dns_sd | Windows 10+ native DNS-SD or Bonjour SDK |
  | Crypto (AES and RSA) | CommonCrypto/Security | CNG, or a portable library such as mbedTLS for both |
  | Start at login | SMAppService, via an Objective-C++ shim | `Run` registry key |

- **Decoding.** ALAC via Apple's open-source decoder (Apache-2.0), which is
  portable. Use AudioToolbox only if clearly better, and always behind the
  interface.
- **Third-party licenses.** Before adding any dependency, verify its license
  is compatible with AGPLv3.
- **v1 platform.** macOS, universal binary (arm64 + x86_64). Propose the
  minimum macOS version with justification; the maintainer's suggestion is
  macOS 13. Bundle ID prefix is `app.tapein`.
- **Name.** Do not use "AirPlay" in the product name. Use it only
  descriptively ("works with AirPlay"), with a notice that there is no
  affiliation with Apple.
- **Distribution** outside the App Store: GitHub Releases, with a signed and
  notarized .dmg.

## 4. Proposed architecture (to be validated in Phase 0)

This is the starting point. It may be challenged, with arguments.

- **`tapein-core`**: pure C++ library with the RAOP receiver, sessions,
  jitter buffer, mixer, resampler and remote control (DACP). No JUCE, no UI.
- **`tapein-platform`**: per-OS implementations of the mDNS, crypto and
  start-at-login interfaces.
- **`Tape In` app (JUCE)**: UI, audio I/O and menu-bar icon. Consumes the
  core.
- **Preparing for v2.** The core's output goes through a lock-free SPSC ring
  buffer. Today it feeds the app's audio I/O. In v2 the same buffer goes to
  shared memory, and the plugin (a JUCE target in the same repo) becomes a
  reader only, with no networking.

**Why the plugin should have no networking.** Opening sockets and
advertising Bonjour from inside the DAW process will probably run into:

- the system firewall;
- the Local Network permission on macOS 15 or newer;
- the sandbox.

Those permissions depend on the DAW, which we do not control. Confirm these
hypotheses in Phase 0.

## 5. Technical points the design must cover

- **Protocol.** Start with AirPlay 1 (RAOP: RTSP + RTP/UDP, advertised as
  `_raop._tcp`).
  - Confirm in practice that current iOS and macOS still send to AirPlay 1
    receivers. shairport-sync itself warns that support is shrinking.
  - Study how shairport-sync, AirBass and UxPlay handle the RSA handshake
    (Apple-Challenge), AES decryption, sync/timing packets and retransmission.
  - Record in the docs any legal or IP question — e.g. the keys used by
    open-source receivers — for the maintainer to decide.
- **Coexistence with the macOS AirPlay Receiver.** macOS 12+ has its own
  AirPlay Receiver, which occupies fixed ports (e.g. 5000 and 7000). Use
  dynamic ports and our own device ID in the Bonjour advertisement, and make
  sure both coexist.
- **Multiple simultaneous sources.** Test in practice how iOS behaves with
  several sessions on the same receiver and design from that.
- **Clocks.** Audio arrives at 44.1 kHz on the sender's clock. The audio
  output runs on its own clock, at 44.1, 48 or 96 kHz. This requires
  high-quality resampling and adaptive drift compensation (driven by buffer
  level), with no clicks.
- **Latency.** AirPlay 1 has about 2 s of buffer imposed by the sender. That
  is acceptable for v1, because auxCord has the same limit. Document it and
  investigate whether it can be reduced.
- **Remote control.** Use DACP (discover the sender's `_dacp._tcp` from the
  DACP-ID and Active-Remote headers) for play/pause, next, previous and
  source volume. Honour the volume the sender sends via RTSP SET_PARAMETER.
  Show metadata (track, artist, artwork) when available.
- **Audio thread:**
  - zero locks, zero allocation, zero I/O or logging;
  - on underrun, fade to silence, never garbage.
- **Network in the core.** It processes packets from the network, so all
  parsing must be defensive: size limits, never trust packet fields. Run the
  tests under sanitizers (ASan/UBSan).

## 6. Work plan

Work in phases. At the end of each phase, deliver:

- a summary of what was done;
- exact instructions for the maintainer to test;
- known problems.

Wait for the maintainer's OK before starting the next phase.

### Phase 0: research and decisions (no product code)

1. Read the code and documentation of shairport-sync, AirBass, UxPlay and
   openairplay/airplay2-receiver.
2. Check each one's license and its compatibility with AGPLv3.
3. Produce `docs/ARCHITECTURE.md` with:
   - the module structure;
   - the platform interfaces;
   - the library choices (crypto, mDNS, resampler);
   - the minimum macOS version;
   - how v2 (plugin) and the Windows port will fit in;
   - the Windows-specific risks (mDNS, firewall, ASIO);
   - the general risks, ordered by impact;
   - open questions for the maintainer.
4. Save this specification as `docs/SPEC.md`.
5. Create a `CLAUDE.md` with the project conventions, to keep context across
   sessions.
6. Stop here and wait for the OK.

### Phase 1: minimal receiver (CLI)

A command-line tool on top of `tapein-core` that advertises a receiver,
accepts a connection from an iPhone and writes the received audio to WAV.

From this phase on, CI on GitHub Actions builds and runs the core tests on
macOS **and Windows**, so portability does not rot.

Acceptance criteria:

- the receiver shows up in Control Center;
- `dns-sd -B _raop._tcp` shows the service;
- 10 minutes of music without dropouts;
- an integration test sends a known WAV and compares it with the received
  file (differences, if any, are explained);
- the core builds on Windows in CI.

### Phase 2: standalone app (JUCE)

Scope: several sources, mixer (volume, mute, solo and meter), audio output
selection, renaming the destination, menu bar, start at login.

Acceptance criteria:

- 2 or more devices playing together;
- switching the audio output without dropping the session;
- 1 hour stable;
- sleep/wake and Wi‑Fi network switch without crash;
- audio reaches Ableton Live via BlackHole and via the interface loopback.

### Phase 3: remote control and metadata

Scope: play/pause, next, previous and volume from the app, plus track name
and artwork when available.

### Phase 4: v1 distribution

- Signed and notarized .dmg. The maintainer provides the Apple Developer
  credentials when we get here.
- README with installation, how to route audio into a DAW, network
  troubleshooting (multicast and client isolation on the router) and known
  limitations.

### Phase 5 (v2): AU + VST3 plugin

Starts only after v1 is stable and the maintainer gives the OK.

1. Scope: JUCE plugin target in the same repo, shared-memory IPC with the
   app, one instance per receiver, state saved in the project.
2. Evaluate shipping two variants, effect and instrument.
3. Acceptance criteria:
   - works in Ableton Live (VST3 and AU) and Logic Pro;
   - 3 instances with different names in the same project;
   - the received audio can be recorded to a track;
   - the project sample rate can be changed;
   - with the app closed, the plugin stays silent and shows a notice, without
     crashing.
4. Mac distribution becomes a .pkg with the app and the plugins.

### Phase 6: Windows

Scope:

- Windows implementations of the platform interfaces;
- WASAPI and ASIO;
- tray icon;
- signed installer;
- VST3 plugin.

The order of Phases 5 and 6 may change. The maintainer decides when we get
there.

## 7. Tests

- **Unit**: ring buffer, jitter buffer, resampler and drift compensation,
  RTSP/RTP parsing (including malformed input), decoding.
- **Automated integration** with an open-source AirPlay sender (e.g. pyatv or
  raop_play), sending a known file and comparing with what was received.
- **Manual matrix run by the maintainer.**
  - Sources:
    - iPhone with Spotify, Apple Music, YouTube and Voice Memos;
    - another Mac, sending system output and a DAW's output over AirPlay;
    - a Windows laptop with an AirPlay sender.
  - Destination: Ableton Live, receiving via BlackHole or the Focusrite
    interface loopback.

## 8. Rules

- Small, descriptive commits, one branch per phase.
- Ask before:
  - adding a dependency whose license is incompatible with AGPLv3;
  - doing anything that uses `sudo` or installs system components;
  - running any step that needs the maintainer's Apple account.
- If anything in this specification is infeasible, or if a better path
  exists, say so and propose the alternative instead of following it to the
  letter.

Start with Phase 0.
