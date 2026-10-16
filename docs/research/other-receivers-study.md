# Tape In — Phase 0: what other open-source AirPlay receivers teach us

Scope: four source trees read as untrusted data (nothing built or run). All paths below are relative to
`R = <repos>`.
Statements not backed by a citation are marked "(inferred)".

Quick orientation: only two of the four actually speak classic RSA-authenticated RAOP (AirBass and, as a side path, airplay2-receiver). UxPlay speaks the FairPlay/ed25519 "legacy AirPlay 2" dialect and has no RSA code at all. The RAOP RSA private key appears byte-for-byte identically in AirBass (`PrivateKey.pem`) and airplay2-receiver (`ap2/airplay1.py`), and in both places it is uncommented.

---

## 1. UxPlay (`R/UxPlay`, v1.74 "Experimental", June 2026)

### a. Licenses
- Top level: GPLv3 (`R/UxPlay/LICENSE:1-2`). `uxplay.cpp:3-10`, `renderers/audio_renderer.c:3-10`, `renderers/video_renderer.c`, `renderers/mux_renderer.c` are GPLv3-or-later; `lib/crypto.c:3-8` and `lib/crypto.h` are GPLv3-or-later (Draschbacher/Ziska).
- The protocol core inherited from shairplay is LGPL-2.1-or-later (Juho Vähä-Herttua 2011-2012): `lib/raop.c:1-6`, `lib/raop_rtp.c`, `lib/raop_buffer.c`, `lib/httpd.c`, `lib/dnssd.c:2-7`, `lib/dns_sd/dns_sd.c`, `lib/pairing.c:2-8`, `lib/netutils.c`, `lib/utils.c`, `lib/threads.h`, `lib/raop_handlers.h`, `lib/fairplay_playfair.c`. Files by dsafa22/fduncanh (`lib/byteutils.c:2-7`, `lib/mirror_buffer.c`, `lib/raop_rtp_mirror.c`, `lib/raop_ntp.c:2-8`, `lib/airplay_video.c:2-8`) say "All Rights Reserved" in the copyright line but carry the same LGPL-2.1+ grant.
- Vendored: `lib/llhttp` MIT (`lib/llhttp/LICENSE-MIT`); `lib/srp.c:6-14` MIT (Tom Cocagne); `lib/playfair/LICENSE.md:2-3` GPLv3 (the .c files have no headers); `lib/mdnsd/mdnsd.c:2-7` LGPL-2.1+ (kgbook, 2026). External: OpenSSL ≥1.1.1, libplist, GStreamer (`lib/CMakeLists.txt:95-140`, `renderers/CMakeLists.txt`).
- Compatibility: everything is MIT / LGPL-2.1+ / GPLv3+ and therefore compatible with AGPLv3. The combined work is GPLv3, so any code copied from UxPlay forces the copying file to be GPLv3-or-later (fine inside an AGPLv3 project).

### b. Protocols and advertisement
- Implements the "Legacy Protocol" of AirPlay 2: RTSP with binary-plist bodies, `/info`, `pair-setup`/`pair-verify`, `fp-setup`, plist `SETUP` with `ekey`/`eiv`, mirroring, audio, and HLS video (`lib/raop_handlers.h:36-1255`, `lib/http_handlers.h`). README: "UxPlay supports Apple's AirPlay2 protocol using 'Legacy Protocol'… While there is no guarantee that future iOS releases will keep supporting 'Legacy Protocol', iOS 17 continues support" (`README.md:229-238`).
- There is no RSA, no `Apple-Challenge`, no `ANNOUNCE`/SDP path: a grep for `RSA|Apple-Challenge|rsaaeskey` across `lib/` and `uxplay.cpp` finds only a stale comment (`lib/raop.c:52`) and the feature bit being switched off: "bit 23: Authentication type 1: RSA Authentication" = 0 (`uxplay.cpp:2420`).
- Advertises both services: `_raop._tcp` named `<MAC>@<name>` (`lib/dns_sd/dns_sd.c:306-317`) and `_airplay._tcp` (`dns_sd.c:378-385`). RAOP TXT keys: ch, cn, da, et, vv, ft, am, md, rhd, pw, sf, sr, ss, sv, tp, txtvers, vs, vn, pk (`dns_sd.c:259-292`) with values `cn=0,1,2,3`, `et=0,3,5` (no RSA "1"), `md=0,1,2`, `tp=UDP`, `sr=44100`, `ss=16`, `vn=65537`, `am=AppleTV3,2`, `vs=220.68`, `ft=0x5A7FFEE6,0x0` (`lib/dnssdint.h:27-44`, `lib/global.h:21-22`). AirPlay TXT: deviceid, features, pw, flags=0x4, model, pk, pi, srcvers, vv (`dns_sd.c:353-375`). Individual feature bits are toggled with commentary in `uxplay.cpp:2405-2440` (bit 27 "Supports Legacy Pairing", bit 30 "RAOP support: with this bit set, the AirTunes service is not required").

### c. Language, architecture, size
- C99 library + C++11 front end (`CMakeLists.txt:9-14`). ~21k lines in `lib/`, `renderers/`, `uxplay.cpp`; ~37k with llhttp and playfair.
- Threads via a pthread-only shim (`lib/threads.h:21-41`; MSYS2 UCRT supplies pthreads, `uxplay.cpp:47`). The RTSP server is a `select()` loop with a fixed table of 12 connections "value used in AppleTV 3" (`lib/httpd.c:157-173`). Each session spawns a UDP RTP thread that polls data+control sockets with a 5 ms `select` timeout (`lib/raop_rtp.c:398-460`), an NTP timing thread (`lib/raop_ntp.c`), and for mirroring a TCP thread. The app side runs a GLib `GMainLoop` (`uxplay.cpp:211,740,832`) because rendering is GStreamer.

### d. Crypto
- OpenSSL EVP only (`lib/crypto.c:26-29`): AES-128-CTR (mirroring), AES-128-CBC without padding for audio (`crypto.c:141-190`; used in `lib/raop_buffer.c:80,133`), AES-128-GCM (encrypted control channel after pair-verify, `crypto.c:278-333`), X25519 ECDH (`crypto.c:192-264`), Ed25519 sign/verify (`crypto.c:345+`, `lib/pairing.c:405-450`), SHA-512 for the pair-verify key derivation with salts "Pair-Verify-AES-Key"/"Pair-Verify-AES-IV" (`pairing.c:29-30`), MD5 for digest auth, SRP-6a for on-screen PIN pairing (`lib/srp.c`, `pairing.c:32-37`).
- The persistent identity is an Ed25519 key in `$HOME/.uxplay.pem` (`uxplay.cpp:1110`, `crypto.c:351-367` via `PEM_read_PrivateKey`); its public half becomes the `pk` TXT value (`lib/raop.c:664-674`). There is no RAOP RSA key anywhere.

### e. mDNS
- Three backends behind one `dnssd_*` API (`lib/dnssd.c`, `lib/dnssdint.h`): (1) `lib/dns_sd/dns_sd.c` against Apple Bonjour on macOS (default), Avahi's `avahi-compat-libdns_sd` on Linux/BSD, or the Windows Bonjour SDK; (2) `lib/mdnsd/` a self-contained minimal mDNSResponder (1,153 lines) that is the new default on non-Apple platforms (`CMakeLists.txt:52-70`, `README.md:5-15`).
- Windows with dns_sd: `dnssd.dll` is loaded at runtime with `LoadLibraryA`/`GetProcAddress` (`dns_sd.c:154-178`) and linked against `C:\Program Files\Bonjour SDK\Lib\x64\dnssd.lib` or `$BONJOUR_SDK_HOME` (`lib/dns_sd/CMakeLists.txt:19-27`). The internal mdnsd enumerates interfaces with `GetAdaptersAddresses` on Windows and `getifaddrs` elsewhere, joins 224.0.0.251/ff02::fb on UDP 5353, and runs its own thread (`lib/mdnsd/mdnsd.c:28-30,351-367,435,520-578,916,1059`).
- macOS extras: AWDL/peer-to-peer registration with `kDNSServiceFlagsIncludeP2P|kDNSServiceFlagsIncludeAWDL` plus the private `SO_RECV_ANYIF` socket option (`dns_sd.c:246-254`, `README.md:1291-1303`).

### f. Audio path
- No ALAC decoder of its own: GStreamer `avdec_alac` / `avdec_aac` (ffmpeg via gst-libav), pipeline `appsrc ! avdec_alac ! audioconvert ! audioresample quality=10 ! volume ! <sink> sync=true|false` (`renderers/audio_renderer.c:35-36,148-178`); ALAC caps carry the magic cookie for 44100/16/2 spf 352 (`audio_renderer.c:61-62`). Default sink `autoaudiosink`; `osxaudiosink`, `wasapisink`, `directsoundsink` selectable (`uxplay.cpp:1084-1086`).
- Jitter buffer: 256-slot ring keyed by RTP seqnum (`lib/raop_buffer.c:36,167-230`); duplicates ignored (`:201-204`), overflow flushes (`:195-197`); missing packets trigger resend requests on the control port (`lib/raop_rtp.c:202-229`); retransmits arrive as type 0x56 (`raop_rtp.c:~492`).
- Timing: the receiver runs an NTP-style exchange against the sender's timing port with a 1 ms precision model and smoothed offset (`lib/raop_ntp.c:46-100`); 0x54 sync packets map rtp↔sender-NTP and the comment records the default sender lead: "next_rtp = sync_rtp + 77175 = 441*175 (1.75 sec) for ALAC", 0.17 s for AAC-ELD (`raop_rtp.c:~508-516`). Each decoded packet gets a sender-NTP time converted to local time and pushed as `GST_BUFFER_PTS` (`uxplay.cpp:2694-2726`, `audio_renderer.c:311-371`); user offsets `-al`/`-ad` are added in nanoseconds (`uxplay.cpp:105-106,1620,1653`). Drift is left to GStreamer's clock/`audioresample`; there is no explicit resampler control in UxPlay itself (inferred from the pipeline string).

### g. Sessions
- One RAOP client at a time. A second RTSP connection while one exists gets `409 Conflict: Server is connected to another client` (`lib/raop.c:283-287`) unless built/run with "nohold", in which case the existing connections are dropped and the newcomer wins (`raop.c:277-282`; `-DNOHOLD` is forced on in `lib/CMakeLists.txt:33-34`).

### h. DACP / metadata
- Reads `DACP-ID` and `Active-Remote` from SETUP and forwards them to an `audio_remote_control_id` callback (`lib/raop_handlers.h:613-621`, `lib/raop_rtp.c:360-363`). It never acts as a DACP client (no `_dacp._tcp` browsing or `/ctrl-int` sends; inferred from grep).
- SET_PARAMETER handles `text/parameters` (`volume:`, `progress:`), `image/jpeg|png` cover art, and `application/x-dmap-tagged` metadata (`raop_handlers.h:1141-1182`); GET_PARAMETER answers `volume` (`:1086-1100`). `uxplay.cpp:2150-2300` walks DMAP tags (`minm`, `asar`, `asal`, …) for display.

### Windows experience (for Tape In's later port)
- Build: MSYS2 with MinGW-w64 — UCRT64 recommended, MINGW64 for "legacy MSVCRT", CLANGARM64 for ARM (`README.md:996-1046`); packages `mingw-w64-ucrt-x86_64-{cmake,gcc,libplist,gstreamer,gst-plugins-base,…}` (`:1044,1056`); OpenSSL comes with MSYS2 (`:1053`).
- Bonjour: before 1.74 users had to install "Bonjour SDK for Windows v3.0" (download links, installs to `C:\Program Files\Bonjour SDK`, `:1003-1011`); now optional via `-DUSE_DNS_SD=1` (`:1000-1001`). Bonjour Service is managed in `services.msc` (`:1840-1841`).
- Firewall: Windows prompts on first run, or "Settings → Windows Security → Firewall & network protection → allow an app through firewall"; antivirus may flag `uxplay.exe` (`:1102-1109`). "If UxPlay starts with no error but is not seen by the client, this is almost always a Windows firewall issue" — add an inbound rule via elevated PowerShell `New-NetFirewallRule … -Profile Private`, ensure the connection is on the Private profile, same Wi-Fi, no AP isolation (`:1111-1131`). The built-in mDNS needs UDP 5353 open (`:588-591`).
- Audio/video sinks: `wasapisink` or `directsoundsink` (`:1135-1137,1526`), `d3d12videosink` recommended (`:1162`). Optional kernel packet timestamps for NTP accuracy, enabled via PowerShell per adapter (`:1185-1197`). The exe can run from Windows Terminal using the full MSYS2 path (`:1167-1171`).

### OS-version notes
- iOS 17 is the newest release named as still supporting the legacy protocol (`README.md:236-238`); no mention of iOS 18/26, Sequoia or Tahoe anywhere in README or sources (grep). "macOS 16" appears once, for enabling the system AirPlay receiver when using `-p2p` (`:1301-1303`). macOS 12 Monterey introduced a native AirPlay receiver "restricted to recent hardware" (`:852-856`).
- macOS Privacy & Security only comes up for installing unsigned GStreamer packages ("Open Anyway", `:891-894`). There is no discussion of the Local Network TCC permission (grep for "Local Network", "TCC", "permission" finds only the firewall and Gatekeeper items).

---

## 2. AirBass (`R/airbass`, jenghis, 2017)

### a. Licenses
- Apache-2.0 (`R/airbass/LICENSE:2-3`, `README.md:30-33`). The three submodules (`.gitmodules:1-9`: abplayercontroller, mediakeys, airtunes — all jenghis, all Apache-2.0 per their READMEs) are empty directories in this shallow clone, so the protocol code must be read from the fork, where every file still carries "Copyright 2017 Jenghis … Apache License, Version 2.0" headers (e.g. `R/AirBass-fork/AirTunes/AirTunes/Public/AirTunes.swift:1-13`). Apache-2.0 is compatible with AGPLv3.
- The only own code present here is `AirBass/AppDelegate.swift` (98 lines), Swift 3.0, deployment target 10.11/10.12 (`AirBass.xcodeproj/project.pbxproj:242,299,302`).

### b-h. See the fork section; the fork did not touch protocol, crypto, mDNS or audio files (identical 2017 headers, no fork markers found by grep).

---

## 3. AirBass fork (`R/AirBass-fork`, megabitsenmzq, 2021 "Megabits fixed ver.")

### a. Licenses
- Top-level Apache-2.0 (`LICENSE`), plus `AirTunes/LICENSE` and `ABPlayerController/LICENSE` (both Apache-2.0).
- Vendored: CocoaAsyncSocket "This class is in the public domain" (`AirTunes/AirTunes/CocoaAsyncSocket/GCDAsyncSocket.h:4-8`) — 15k lines; `MediaKeys/AppleRemote.{h,m}` is Martin Kahr's MIT code *with VideoLAN modifications under "GPL version 2, or (at your option) any later version"* (`MediaKeys/AppleRemote.m:6-41`). "GPLv2 or later" is AGPLv3-compatible, but this file is the one piece that is not permissive; it is also irrelevant to Tape In (IR remote).
- `AirTunes/AirTunes/PrivateKey.pem` (23 lines, raw `-----BEGIN RSA PRIVATE KEY-----`, no comment, no attribution) is the well-known AirPort Express key; it is bundled as an app resource (`AirTunes/AirTunes.xcodeproj/project.pbxproj:34,191,278`) and read at runtime (`Private/SessionManager.swift:33-35`).

### b. Protocol and advertisement
- Classic RAOP only. Publishes `_raop._tcp.` via Foundation `NetService` with name `<hex MAC>@AirBass` on TCP 5001 (`Private/BonjourService.swift:22-36,42-43`). TXT: `et=1, sf=0x4, tp=UDP, vn=3, cn=1, md=0,1,2` (`BonjourService.swift:46-49`). No `_airplay._tcp`, no `pk`, no `ft`. The MAC is hard-coded "to avoid the convoluted lookup process" (`Private/SessionManager.swift:42-46`).
- RTSP methods handled: OPTIONS (Apple-Challenge), ANNOUNCE body as SDP, SETUP (fixed `server_port=6010;control_port=6011`, no timing port — `Private/RTSP/RTSPResponse.swift:28-33`), RECORD/FLUSH, TEARDOWN, SET_PARAMETER bodies by content type (`Private/RTSP/RTSPSession.swift:21-27,101-115,134-152`).

### c. Language, architecture, size
- Swift 5.0 (`AirBass.xcodeproj/project.pbxproj:352`; Xcode 12 `LastUpgradeCheck = 1200`, `:155`), min macOS 10.11 (`:272,345`). Own code: ~1,000 lines AirTunes, ~1,000 ABPlayerController, ~1,000 MediaKeys.
- Architecture: no explicit threads; GCD queues `tcpQueue`, `UDPQueue`, `CallbackQueue` with `GCDAsyncSocket`/`GCDAsyncUdpSocket` delegates (`RTSPSession.swift:78-83`, `Private/RTP/RTPSession.swift:19,52-59`, `Private/AudioSession.swift:19,38-40`). Module layout: `AirTunes` (framework: Bonjour, RTSP, RTP, crypto, AudioQueue, DACP) → exposes `ABPlayerService` (`ABPlayerController/ABPlayerInterface/ABPlayerService.swift:19-34`); `ABPlayerController` (NSViewController UI with expanded/collapsed views, dark mode, floating window, `Public/ABPlayerController.swift:18-348`); `MediaKeys` (CGEvent tap for play/next/prev keys, `MediaKeys/EventTap.swift:29`); `AirBass/AppDelegate.swift` wires them (`:39-52`).

### d. Crypto (Security.framework + CommonCrypto)
- Key load: PEM base64 body regex-stripped and fed to `SecKeyCreateFromData([kSecAttrKeyType: kSecAttrKeyTypeRSA, kSecAttrKeyClass: kSecAttrKeyClassPrivate])` (`Private/CryptoService.swift:32-50`).
- Apple-Challenge: challenge bytes + local socket IP + hardware address, zero-padded to 32 (`RTSPSession.swift:154-162`), then `SecSignTransformCreate` with `kSecInputIsRaw` (i.e. raw PKCS#1 v1.5 signature with no hash) (`CryptoService.swift:52-56,62-64`).
- Session key: SDP `rsaaeskey`/`aesiv` (`Private/RTSP/SDPParser.swift:27-29`), decrypted with `SecDecryptTransformCreate` + `kSecPaddingOAEPKey` (`CryptoService.swift:57-61`). So: SecTransform, not `SecKeyRawSign`/`SecKeyCreateSignature`. Security Transforms are deprecated on current macOS (inferred); the modern equivalents are `SecKeyCreateWithData`, `SecKeyCreateSignature(.rsaSignatureRaw)`, `SecKeyCreateDecryptedData(.rsaEncryptionOAEPSHA1)`.
- Payload: AES-128-CBC via `CCCryptorCreate(kCCDecrypt, 0 /*AES*/, 0 /*CBC, no padding*/ …)` + `CCCryptorUpdate`; trailing partial block appended in clear (`Private/RTP/DecryptedPacket.swift:37-50`).

### e. mDNS and DACP
- `NetService` publish (`BonjourService.swift:28-37`); `NetServiceBrowser` for `_dacp._tcp` in `local.`, matching the service whose name contains the `DACP-ID`, then HTTP `GET /ctrl-int/1/{playpause|pause|nextitem|previtem}` with `Active-Remote: <token>` (`Private/RemoteService.swift:33-38,41-47,65-68,117-120,127-135`). `NetService`/`NetServiceBrowser` are deprecated in favour of Network.framework (inferred).

### f. Audio path
- Output: AudioToolbox `AudioQueueNewOutputWithDispatchQueue` with `kAudioFormatAppleLossless`, 44100 Hz, 352 frames/packet, 2 channels, and the ALAC magic cookie set via `kAudioQueueProperty_MagicCookie` — the system decodes ALAC (`AudioSession.swift:73-96,279-286`). Three 2,048-byte buffers with packet descriptions (`:98-108,264-265`).
- Jitter buffer: 1,024-entry ring indexed by seqnum (`AudioSession.swift:110-138,270-274`); playback starts once 2.0 s of audio is queued (`:140-147,158-173,284`); missing packets are skipped (`:204-214`); gaps < 128 trigger resend requests `[0x80,0xD5,0x00,0x01,seq,count]` on the control port (`RTPSession.swift:74-92,104-121`). Retransmits (payload 86) parsed at a 16-byte header offset (`Private/RTP/AudioPacket.swift:22-23,66-89`).
- No timing port, no NTP, no clock-drift compensation, no resampler: the AudioQueue consumes at device rate and the 2 s pre-roll is the only latency control. Enqueue failure pauses and resynchronises read=write index (`AudioSession.swift:242-260`).
- Volume: the fork maps RAOP volume (-30…0 dB) to 0…1 and sets the *system default output device* master volume via CoreAudio `AudioObjectSetPropertyData(kAudioHardwareServiceDeviceProperty_VirtualMasterVolume)` (`ABPlayerController/ABPlayerController/Public/ABPlayerController+volume.swift:12-45`, called from `ABPlayerController.swift:253`).

### g. Sessions
- "Last SETUP wins": on SETUP all other accepted TCP sockets are disconnected (`RTSPSession.swift:117-123`). There is no 453 Not Enough Bandwidth/busy response.

### h. Metadata
- `application/x-dmap-tagged` → `DAAPParser`, `image/jpeg` → `NSImage` artwork, `text/parameters` → `volume:` and `progress:` regexes (`RTSPSession.swift:175-196`, `Private/RTSP/ParameterParser.swift:27-64`); KVO on `TrackInfo`/`PlayerInfo` notifies the UI (`Public/AirTunes.swift:58-83`).

### What the 2021 fork changed
- Toolchain: Swift 3 → 5 (`pbxproj:352,457`), Xcode 12 upgrade check, submodules inlined, bundle id `com.megabits.airbass`, new icon and `AirBase.sketch`.
- MediaKeys rewritten as an `NSApplication` subclass installed as `NSPrincipalClass` (`AirBass/Info.plist:29-30`, `MediaKeys/MediaKeys.swift:26-45`), adding VLC's `AppleRemote` IR support and a changed delegate signature (`AirBass/AppDelegate.swift:44-46,81-95` vs original `:81-96`), plus `NX_KEYTYPE_NEXT/PREVIOUS`.
- System-volume control added (above). README relabelled "Megabits fixed ver." and "original project … last update 4 years ago" (`README.md:1-8`).
- Nothing in RTSP/RTP/crypto/Bonjour changed. The fixes were about building on Xcode 12/Swift 5, not about newer iOS/macOS protocol behaviour (inferred from identical headers and absence of any protocol edits).
- Would it still build today? Conceptually yes: the APIs used (SecTransform, NetService, AudioQueue, CGEvent taps) still exist on macOS 15 with deprecation warnings; a modern build would need the Hardened Runtime/notarization and, if sandboxed, the network-server entitlement, and an Accessibility prompt for the event tap (inferred). Whether current iOS senders will still pick an `et=1`-only receiver with no `pk`/`ft` keys is not evidenced in either repo.

---

## 4. airplay2-receiver (`R/airplay2-receiver`, openairplay, Python)

### a. Licenses
- No top-level LICENSE; no license text in README. **No license = all rights reserved**: the bulk (`ap2-receiver.py`, `ap2/pairing/hap.py`, `ap2/connections/*.py`, `ap2/bitflags.py`, `ap2/utils.py`, `ap2/sdphandler.py`, `fp_decrypt.py`) may be read but not copied.
- Per-file exceptions: `ap2/airplay1.py:1-17` is LGPL-2.0-or-later (SIFTeam OpenAirPlay, 2012) — compatible with AGPLv3; `ap2/dxxp.py:1-4` "License: GPLv2" and `ap2/fairplay3.py:1-4` "GPLv2" (systemcrash 2021/2022, a port of OmgHax) — GPLv2-only, **not** compatible with AGPLv3; `ap2/pairing/srp.py:5` "Inspired from pyhomekit" (no license).
- Dependencies (`requirements.txt`): zeroconf, pycryptodome, cryptography, hkdf, srptools, av (PyAV/ffmpeg), pyaudio.

### b. Protocols and advertisement
- AirPlay 2: HomeKit transient and non-transient pair-setup (SRP M1-M4, `ap2/pairing/hap.py:474-547`), pair-verify with X25519/Ed25519 and ChaCha20-Poly1305 + HKDF (`hap.py:8-15,549`), FairPlay v3 (`ap2/fairplay3.py`), REALTIME (payload 96) and BUFFERED (103) streams (`ap2/connections/stream.py:12-13`), RTCP time announcements for NTP (0xD4) and PTP (0xD7) parsed (`ap2/connections/control.py:22-83`) though "Accurate audio sync (with help of PTP and/or NTP)" is explicitly not implemented (`README.md:24-26`), `SETPEERS`, `SETRATEANCHORTIME`, `FLUSHBUFFERED`, `SETMAGICCOOKIE` ("first appeared during iOS17 betas", git `6c343d3`).
- Classic RAOP kept as a side path: `OPTIONS` answers `Apple-Challenge` with `Apple-Response` and `Apple-Jack-Status` (`ap2-receiver.py:398-416`), `ANNOUNCE` parses SDP and builds the AES key from `rsaaeskey` or the FairPlay key message (`:418-462`), `SETUP` after ANNOUNCE reads `Transport:` and derives latencyMin/Max from the sample rate (`:493-517`).
- Advertises only `_airplay._tcp.local.` on port 7000 using python-zeroconf (`ap2-receiver.py:1192-1203`); the RAOP keys are commented out inside the same dict (`:255-299`). TXT includes acl, deviceid, features, flags, gcgl, gid, manufacturer, model, name, protovers, rsf, serialNumber, srcvers, pi, pk, sf (`:231-285`).

### c. Language, architecture, size
- Python 3 (~12.6k lines, of which 6,170 are `fairplay3.py`). `socketserver.ThreadingTCPServer` with `allow_reuse_address` (`:1233-1237`); audio streams run in `multiprocessing` processes (`ap2/connections/audio.py:2`); README: "multiple concurrent connections. There are no safeguards built to prevent you playing multiple streams" (`README.md:46-53`).

### d. Crypto
- pycryptodome RSA for AP1: a hand-written CRT signature over an `00 01 FF…00 || challenge || ip || mac` PKCS#1 block (`ap2/airplay1.py:72-118`) and `PKCS1_OAEP` for `rsaaeskey` (`ap2/playfair.py:1-2`). The AirPort private key is inline as `AIRPORT_PRIVATE_KEY` (`airplay1.py:25-48`, duplicated in `playfair.py:8-10`), with no comment beyond the docstring test vectors (`airplay1.py:51-70`). `cryptography` for X25519/Ed25519, pycryptodome ChaCha20-Poly1305, `hkdf`, SRP via its own `srp.py`.

### e. mDNS
- python-zeroconf `ServiceInfo` (`:1195-1203`); README warns that on macOS both mDNSResponder and zeroconf answer, causing `NonUniqueNameException`, and that the built-in AirPlay Receiver must be disabled on Ventura+ (`README.md:56-60,100-101`).

### f. Audio path
- PyAV/ffmpeg codecs `alac`, `aac`, `opus`, `pcm_s16le_planar`, `pcm_s24le` with explicit ALAC extradata built per rate/bit depth (`ap2/connections/audio.py:354-380,467-488`), `av.AudioResampler` to the output format (`:508-511`), PortAudio/pyaudio output (`:454-457`). Latency compensation = device low-output latency + stream output latency + codec latency + a fixed 2 ms "ptpDelay" (`:514-521`). `RTPRealtimeBuffer` does seqnum rollover sorting, gap detection and retransmit requests (`:146-320`, `control.py:109-114`).

### g/h. Sessions, remote control
- Concurrent senders allowed with no arbitration (`README.md:46-53`). DACP is listed under "Next steps" (`:41`); DMAP metadata parsed by `ap2/dxxp.py` (GPLv2).

### What it documents about the features bitfield and keeping RAOP alive
- `ap2/bitflags.py:59-146` names every bit and records empirical sender behaviour: Ft09 "necessary for iPhones/Music: audio"; Ft12 "iTunes4Win ends ANNOUNCE with rsaaeskey, does not attempt FPLY auth; also coerces frequent OPTIONS packets (keepalive) from iPhones"; Ft14 "Music on iPhones needs this"; Ft18 "macOS needs 18 to pair"; Ft19 "macOS needs 19" (ALAC); Ft20 "iOS needs 20" (AAC-LC); Ft22 "Try Ft22 without Ft40 - ANNOUNCE + SDP"; Ft23 RSA_Auth; Ft25 "seems to require ANNOUNCE"; Ft27 LegacyPairing "connects and works OK"; Ft40 buffered audio; Ft41 PTP; Ft46/48 HomeKit/transient pairing. Default AP2 set at `:152-165`.
- `ap2-receiver.py:58-67`: the `srcvers` value steers the sender — "≤355 to trigger REALTIME and NTP (as opposed to buffered)", "≥355 to trigger PTP and buffered", "200 to trigger ANNOUNCE (APv1)"; and `:253` "absence [of srcvers] triggers AP1 ANNOUNCE behaviour". `:503-504`: "Run receiver with bit 13/14 and no bit 25, it's RSA in ANNOUNCE. Sender assumes you are an airport with only 250 msec buffer, so min/max are absent from SDP."

### Why AirPlay 2 is out of scope for Tape In v1
HomeKit pairing (SRP + persistent LTPK/LTSK store, `hap.py:131-330`), encrypted control channel, PTP grandmaster election, buffered-audio flow control (`SETRATEANCHORTIME`, `FLUSHBUFFERED`) and FairPlay v3 are each a project in themselves, the FairPlay port is GPLv2-only, and MFi is impossible without hardware (`README.md:30-31`). None of this is needed for a classic RAOP target; the only lesson to carry is which TXT/feature bits make modern senders choose the AP1 path.

---

## What Tape In should borrow, and from where

1. **Two-service advertisement with full RAOP TXT set** — mirror UxPlay's key list and values (`R/UxPlay/lib/dns_sd/dns_sd.c:259-292`, `lib/dnssdint.h:27-44`); add `et=1` and consider `srcvers`-less / low `vs` to steer senders to ANNOUNCE (`R/airplay2-receiver/ap2-receiver.py:58-67,253`).
2. **Feature-bit cheat sheet** — use `R/airplay2-receiver/ap2/bitflags.py:59-146` and `R/UxPlay/uxplay.cpp:2405-2440` as the reference when choosing `ft`; bit 23 (RSA auth) is what Tape In turns *on* and UxPlay turns off.
3. **dns_sd abstraction with runtime-loaded `dnssd.dll`** — `R/UxPlay/lib/dns_sd/dns_sd.c:110-217` is the exact pattern for a Windows build that degrades gracefully when Bonjour is absent.
4. **Fallback built-in mDNS responder** — `R/UxPlay/lib/mdnsd/` (LGPL-2.1+) shows a 1.2k-line responder that removes the Bonjour-SDK dependency on Windows; keep it as a Phase-2 option.
5. **Apple-Challenge construction** — challenge ‖ local IP ‖ MAC, zero-pad to 32, raw PKCS#1 sign (`R/AirBass-fork/AirTunes/AirTunes/Private/RTSP/RTSPSession.swift:154-162`; PKCS#1 block layout in `R/airplay2-receiver/ap2/airplay1.py:90-97`).
6. **Security.framework usage on macOS** — AirBass proves the chain works with Apple APIs only (`CryptoService.swift:44-65`); Tape In should use the non-deprecated `SecKeyCreateWithData`/`SecKeyCreateSignature`/`SecKeyCreateDecryptedData` equivalents, or OpenSSL/mbedTLS for a single cross-platform path (UxPlay's EVP wrappers in `R/UxPlay/lib/crypto.c:46-190`).
7. **AES-CBC with trailing plaintext bytes** — `R/AirBass-fork/.../DecryptedPacket.swift:34-51` and `R/UxPlay/lib/raop_buffer.c:125-140` both document that the last partial block is unencrypted.
8. **Seqnum ring jitter buffer + resend protocol** — `R/UxPlay/lib/raop_buffer.c:36-250` (256 slots, dedupe, flush) and resend request format `R/UxPlay/lib/raop_rtp.c:202-229` / `R/AirBass-fork/.../RTPSession.swift:104-114`.
9. **Retransmit packet layout** — type 0x56 with a 4-byte prefix before the inner RTP header (`R/AirBass-fork/.../AudioPacket.swift:66-89`, `R/UxPlay/lib/raop_rtp.c:~492-500`).
10. **NTP timing thread and 0x54 sync packets** — `R/UxPlay/lib/raop_ntp.c:46-100,437-485` and `lib/raop_rtp.c:~508-540` (including the 1.75 s ALAC / 0.17 s AAC default lead). AirBass's lack of this is why it needs a blunt 2 s pre-roll.
11. **Latency as sender-NTP → local-time PTS** — `R/UxPlay/uxplay.cpp:2694-2726` + `renderers/audio_renderer.c:311-371`; Tape In's JUCE output should schedule by converted timestamps the same way.
12. **Single-sender policy with explicit 409 and optional takeover** — `R/UxPlay/lib/raop.c:270-290`; AirBass's socket-dropping takeover (`RTSPSession.swift:117-123`) is the simpler alternative.
13. **SET_PARAMETER dispatch by Content-Type** — `R/UxPlay/lib/raop_handlers.h:1141-1182` and `R/AirBass-fork/.../RTSPSession.swift:21-27,47-64`; DMAP tag walker in `R/UxPlay/uxplay.cpp:2150-2300`.
14. **DACP client** — `R/AirBass-fork/.../RemoteService.swift:33-68,117-143` is a complete, tiny `_dacp._tcp` browse + `/ctrl-int/1/<cmd>` implementation.
15. **ALAC decode via the OS** — AirBass shows AudioQueue decodes ALAC natively given the magic cookie (`AudioSession.swift:92-96,285`); on macOS Tape In can use AudioToolbox's converter instead of shipping a decoder; `R/UxPlay/renderers/audio_renderer.c:61-62` gives the same cookie as caps.
16. **Volume mapping** — RAOP -30…0 dB (and -144 mute) → linear (`R/AirBass-fork/.../ABPlayerController+volume.swift:13`, `R/UxPlay/lib/raop_handlers.h:1146-1153`); prefer an in-app gain (UxPlay's `volume` element) over AirBass's system-volume hijack.
17. **Windows runbook** — `R/UxPlay/README.md:996-1197`: MSYS2/UCRT64, firewall allow + `New-NetFirewallRule`, Private profile, WASAPI sink; reuse this verbatim as the skeleton of Tape In's Windows docs.
18. **Header-size and CSeq hardening** — `R/UxPlay/lib/raop.c:226-250` (431 on oversized headers, reject non-numeric CSeq).
19. **Persisted key handling** — UxPlay stores its identity in `$HOME/.uxplay.pem` (`uxplay.cpp:1110`, `crypto.c:351-367`); Tape In should keep the RAOP RSA key as a compiled-in resource (as AirBass does) but document its provenance, which neither project does.
20. **Keep-alive/feature quirk notes** — `R/airplay2-receiver/ap2/bitflags.py:76-80` (Ft12 drives frequent OPTIONS keepalives from iPhones) and `ap2-receiver.py:503-504` (sender assumes a 250 ms AirPort buffer when min/max latency are absent from SDP).

## Licensing conclusions

- **May be ported (with attribution and headers kept):** UxPlay's LGPL-2.1+ core (`lib/raop*.c`, `lib/httpd.c`, `lib/dnssd*.c`, `lib/dns_sd/`, `lib/mdnsd/`, `lib/pairing.c`), its MIT `lib/srp.c` and `lib/llhttp`; UxPlay's GPLv3+ files (`lib/crypto.c`, `renderers/*`, `uxplay.cpp`, `lib/playfair/`) — AGPLv3 may incorporate GPLv3 code, but the resulting files stay GPLv3-or-later; AirBass original and fork Apache-2.0 Swift (`AirTunes`, `ABPlayerController`, `MediaKeys.swift`, `EventTap.swift`) — Apache-2.0 is compatible with AGPLv3 (keep NOTICE/attribution); CocoaAsyncSocket (public domain); `airplay2-receiver/ap2/airplay1.py` (LGPL-2.0+).
- **May be read for understanding only:** everything in `airplay2-receiver` without a header (`ap2-receiver.py`, `hap.py`, `connections/*`, `bitflags.py`, `utils.py`, `sdphandler.py`, `fp_decrypt.py`) — no license means all rights reserved; use it as documentation of observed behaviour, re-derive any tables (feature bits are facts, not expression, but copy the facts, not the file).
- **Must not be copied:** `airplay2-receiver/ap2/dxxp.py` and `ap2/fairplay3.py` (GPLv2-only, incompatible with AGPLv3); `AirBass-fork/MediaKeys/AppleRemote.{h,m}` is GPLv2-or-later (compatible) but unrelated to Tape In, so leave it out rather than carry a dual MIT/GPL file.
- **The RSA key:** present uncommented in `AirBass-fork/AirTunes/AirTunes/PrivateKey.pem` and `airplay2-receiver/ap2/airplay1.py:25-48` / `ap2/playfair.py:8-10`. It is not copyrightable expression of either project; Tape In's decision to embed it is a separate (reverse-engineering/trademark-neutral) policy question, not a license question. UxPlay deliberately contains no RSA key.
- **Practical rule:** write Tape In's C++20 core fresh, using UxPlay (LGPL/GPL) as the structural reference and AirBass (Apache) as the RAOP-specific reference; any literal code lifted from UxPlay goes into clearly marked GPLv3+ files with original copyright lines.
