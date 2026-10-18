# Phase 3 — Remote control & now-playing metadata

Built 2026-10-17 on `phase-3/remote-metadata`. Adds now-playing metadata
(track, artist, album, artwork, progress) pushed from the sender, remote
control sent back to the sender (play/pause, next, previous), and the
neumorphic vertical UI redesign.

## What it does

- **Metadata**: the sender pushes DMAP-tagged metadata, JPEG/PNG artwork and a
  `progress:` line over RTSP `SET_PARAMETER`. A pure, fuzzed DMAP parser
  (`core/session/dmap.*`) reads `minm`/`asar`/`asal`; the Engine caches it per
  source and exposes it on `SourceSnapshot` (`trackName`, `artist`, `album`,
  `artwork`, `elapsedSeconds`, `durationSeconds`). Hostile input never reads
  out of bounds; strings are clamped to 256 bytes; artwork over 1 MB is dropped.
- **Remote control**: the app resolves the sender's DACP server
  (`iTunes_Ctrl_<DACP-ID>` on `_dacp._tcp`, via a new macOS `DnsSdResolver`)
  and sends `GET /ctrl-int/1/{playpause,nextitem,previtem}` carrying the
  captured `Active-Remote` token. A non-live or token-less source is a no-op;
  resolve/connect failures are dropped without stalling the reactor.
- **UI**: a vertical neumorphic window — top bar (Boldonse "TAPE IN" wordmark,
  output-device selector, settings gear), a scrolling list of source slots
  (album artwork, track/artist/device, state pill, prev/play-pause/next
  transport buttons, a volume fader for local mix gain, solo/mute), and a
  buffer-mode segmented footer. The gear folds down a settings panel (network
  name, start-at-login, ignore sender volume).

## Build & run

```
cmake -S . -B build -G Ninja -DTAPEIN_BUILD_APP=ON
cmake --build build --target TapeIn
open "build/app/TapeIn_artefacts/Debug/Tape In.app"
```

Unit + fuzz tests: `cmake --build build && ctest --test-dir build` (206 tests).
The mDNS resolver test is gated: `TAPEIN_RUN_MDNS_TESTS=1 ctest --test-dir build -R DnsSdResolver`
(macOS; a Terminal CLI is exempt from the Local Network permission).

## Maintainer acceptance test (iPhone)

1. Launch the app; allow the Local Network permission on first run.
2. On the iPhone, pick "Tape In" under AirPlay and play a track (Apple Music
   or Spotify). Audio should come out of the Scarlett within ~2 s.
3. **Metadata**: the slot should show the track title, artist and album, the
   album artwork, and a green "Playing" pill. Change tracks on the phone — the
   slot should follow.
4. **Remote control**: press **play/pause**, **next** and **previous** in the
   app. The iPhone should respond (pause, skip, go back). This needs the phone
   reachable on the LAN (`_dacp._tcp`); if the phone blocks it, the buttons are
   a safe no-op.
5. **Volume fader**: drag a slot's fader — the level you hear should change
   (local mix gain).
6. **Settings**: open the gear, rename the receiver, confirm the new name
   appears under AirPlay on the phone.

Report what works and what doesn't; that's the Phase 3 gate.

## Known limitations

- Remote control targets the sender's DACP server; a sender that doesn't run
  one (or blocks `_dacp._tcp`) simply won't respond — the commands are dropped.
- The DACP connect is a short blocking connect on the reactor thread; a sender
  that is resolvable but silently offline could briefly delay the reactor until
  the OS refuses the connection. Fine on a healthy LAN; revisit with an async
  connect if it bites.
- Sending volume *to the sender* (`Engine::setSenderVolumeDb`, DACP
  `dmcp.device-volume`) is implemented in the core but not wired to a UI
  control this phase; the slot fader is local mix gain.
- auxCord's "Restrict access" and "Exclusive solo" settings are out of scope
  for this phase (no backing behavior yet).
- The macOS resolver has no Windows/Linux counterpart yet; on those platforms
  remote control is a no-op (metadata and the UI still work).
