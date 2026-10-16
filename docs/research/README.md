# Phase 0 research notes

Raw research reports that back the decisions in [../ARCHITECTURE.md](../ARCHITECTURE.md).
Written 2026-10-16. Paths cited inside the first two reports refer to shallow
clones of the upstream repositories at that date; line numbers will drift.

| File | What it covers |
|---|---|
| `shairport-sync-study.md` | RAOP protocol mechanics as implemented by shairport-sync: Bonjour TXT, RTSP handshake, RSA/AES, RTP, resends, timing, buffering, sessions, DACP, defensive-parsing checklist |
| `other-receivers-study.md` | UxPlay, AirBass (+2021 fork), openairplay/airplay2-receiver: licenses, what each implements, what may be borrowed |
| `airplay1-status-legal-daw.md` | Does current iOS/macOS still stream to classic receivers; RSA key provenance and legal history; trademark; Local Network privacy and DAW plugin hosting; built-in macOS receiver; test senders; multi-sender; latency |
| `platform-and-libraries.md` | JUCE 9 licensing and CMake, minimum macOS, SMAppService, DNS-SD on macOS/Windows, crypto options, ALAC, resamplers, plugin SDK licenses, IPC for v2, CI runners, notarization |
| `local-observations-2026-10-16.md` | What was observed on the maintainer's Mac: Bonjour records of auxCord and the built-in receiver, auxCord bundle metadata |
