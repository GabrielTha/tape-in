# Tape In — Architecture and Phase-0 decisions

Status: **draft for maintainer review** (Phase 0). Date: 2026-10-16.
Scope and phases are in [SPEC.md](SPEC.md). Conventions are in
[../CLAUDE.md](../CLAUDE.md).

Everything here was derived from: the source of shairport-sync (MIT), UxPlay
(LGPL/GPL), AirBass (Apache-2.0) and openairplay/airplay2-receiver (no
license); Apple, Microsoft, JUCE and Steinberg documentation; and direct
observation of the maintainer's machine, where auxCord 2.6.1 was running next
to the built-in macOS AirPlay Receiver. Claims that are inferred rather than
read are marked *(inferred)*. Decisions that need the maintainer's answer are
collected in §13.

---

## 0. Decisions at a glance

| Topic | Decision | Why (short) | Confidence |
|---|---|---|---|
| Protocol | AirPlay 1 / RAOP only (RTSP + RTP/UDP, `_raop._tcp`) | What auxCord does today; AirPlay 2 needs HomeKit pairing, PTP, FairPlay — out of scope | High |
| Framework | JUCE **9.0.4** under AGPLv3 (not JUCE 8; see §13 Q1) | JUCE 8 went security-only in July 2026; 9 has the rewritten CoreAudio layer | Medium |
| Min macOS | **13.0** deployment target; 14+ "tested" | 13 is the API floor (SMAppService); nothing in v1 needs 14 | Medium |
| Crypto | **Mbed TLS 3.6 LTS** inside the core (Apache-2.0) | One code path on macOS/Windows/Linux CI; RSA raw-sign, OAEP-SHA1, AES-CBC all 1:1 | High |
| ALAC | Apple's reference decoder (Apache-2.0) behind a hardened wrapper | Spec preference; portable C++; complete bitstream support | High |
| Resampler | **libsamplerate 0.2.2** (BSD-2), sinc, variable ratio | Smooth per-block ratio changes; `process()` verified allocation-free | High |
| mDNS | macOS: `dns_sd.h`. Windows: native `windns.h` DNS-SD (10 1809+), mjansson/mdns as fallback | No Bonjour redistribution problem, no extra firewall rule | Medium-High |
| Login item | macOS: `SMAppService.mainApp` (Obj-C++). Windows: `HKCU\...\Run` | Only supported API on 13+ | High |
| RSA key | Must be embedded; cannot be bypassed (see §9) | Apple-Challenge is answered on every request regardless of `et` | High |
| Sessions | Many concurrent senders; one audio stream per *client* (DACP-ID), no 453 | auxCord parity; protocol has no limit, it is receiver policy | Medium |
| Clocking | Buffer-level-driven adaptive resampling per source; sender NTP used for diagnostics only in v1 | Keeps latency constant without tying the audio thread to network clocks | Medium-High |
| Ports | RTSP TCP on a dynamic port; 3 UDP ports per session, dynamic | Coexist with macOS receiver (5000/7000) and auxCord | High |
| Plugin (v2) | Reader of a transport-neutral SPSC ring; no sockets in the DAW | Local Network permission and firewall are attributed to the DAW, not us | Medium |
| Windows audio | WASAPI shared + exclusive baseline; ASIO via JUCE (SDK now GPLv3-or-proprietary) | Steinberg relicensed ASIO SDK in Oct 2025 | High (license), Medium (details) |

---

## 1. Module structure

```
tapein/
├── core/          libtapein_core  (C++20, no JUCE, no Obj-C, no platform UI)
│   ├── net/       socket wrapper (POSIX/Winsock), poll reactor, timers
│   ├── rtsp/      request parser/serializer, state machine, SDP parser
│   ├── rtp/       audio/control/timing packet parsers, resend requests
│   ├── crypto/    AppleChallenge, RsaAesKeyUnwrap, AesCbcDecryptor (Mbed TLS)
│   ├── codec/     AlacDecoder wrapper (fixed buffers, bounds-checked)
│   ├── buffer/    JitterBuffer (seq-indexed), SpscRing (PCM + header)
│   ├── session/   Client, Session, Receiver (orchestration), policy
│   ├── dsp/       Resampler (libsamplerate), DriftController, Mixer, Meter
│   ├── remote/    DacpClient (HTTP), DmapParser (metadata)
│   └── platform/  interfaces only: IServiceAdvertiser, IServiceBrowser,
│                  ILoginItem, IClock, ILogSink
├── platform/      libtapein_platform
│   ├── macos/     dns_sd advertiser/browser, SMAppService shim (.mm)
│   └── windows/   windns advertiser/browser, registry Run key
├── app/           JUCE 9 app "Tape In" (menu bar, audio device, UI)
├── tools/         tapein-cli (Phase 1: receive → WAV), replay/fuzz helpers
├── tests/         unit (Catch2), integration (sender scripts), fuzz corpora
├── third_party/   mbedtls, alac, libsamplerate, catch2 (+ THIRD_PARTY.md)
└── docs/
```

Dependency direction is strictly downward: `app → platform → core`,
`tools → platform → core`, `tests → core (+platform)`. The core never links
JUCE, never includes Objective-C, and builds on Linux so that CI can run its
sanitizer suite on the cheapest runner.

### Threads (v1 app)

| Thread | Owns | Talks to |
|---|---|---|
| **Network** (one, `poll()`-based reactor) | RTSP listener + all RTSP connections, all RTP UDP sockets, timing exchange, resend timers, mDNS socket (`DNSServiceRefSockFD`), DACP HTTP client, ALAC decode | Writes decoded PCM into per-session JitterBuffer; releases in-order frames into per-session SpscRing |
| **Audio** (JUCE device callback) | Per-session Resampler + DriftController, gain/mute/solo, Mixer, Meter | Reads SpscRings; publishes meters and buffer stats via atomics |
| **UI / message** (JUCE) | Source list, settings, menu bar | Receives events through a lock-free queue from Network; writes parameters as atomics |

Why one network thread: 100 sessions × 3 UDP sockets is trivial for `poll`;
ALAC decode of a 352-frame packet costs tens of microseconds; a single
reactor removes all locking between connections. If profiling ever shows a
problem, sessions can be sharded across N reactors without API change.

The core exposes two surfaces: a **control API** (start/stop receiver,
rename, list clients, send remote commands, subscribe to events) used from
the UI thread, and a **render API** (`Mixer::render(float* const* out, int
frames)`) called from the audio thread. Nothing else crosses threads.

---

## 2. Data path (one session)

```
UDP 0x60/0x56 ──► parse (len checks) ──► AES-128-CBC decrypt (IV reset per packet,
tail bytes clear) ──► ALAC decode → 352 frames S16 stereo ──► JitterBuffer[seq % N]
                                                                     │
   resend requests (0x80 0xD5, to sender control port) ◄── gap detector
                                                                     │ release task
                                                                     ▼ (in-order, due frames)
                                  SpscRing (44.1 kHz, sender-clocked PCM, with header)
                                                                     │ audio thread
                                           Resampler (ratio ← DriftController(fill level))
                                                                     ▼
                                     gain · mute · solo · meter ──► Mixer ──► device
```

- **JitterBuffer**: 1024 slots × 352 frames (≈ 8 s), indexed by 16-bit
  sequence with wrap-around arithmetic, each slot `{seq, rtptime, state}` with
  atomic state. Written only by the network thread.
- **Release task** (network thread, every 10 ms): moves contiguous in-order
  slots into the ring. A missing slot whose resend window has expired (first
  request after 100 ms, repeat every 250 ms, give up 100 ms before it is
  due — the shairport-sync constants) is replaced by silence with a short
  ramp. This keeps the ring strictly in order and makes the audio thread a
  pure consumer.
- **SpscRing**: single producer/single consumer, power-of-two frames, header
  `{magic, version, sampleRate, channels, writePos, readPos, generation,
  heartbeat}` laid out so the same code can run over heap memory (v1) or a
  memory-mapped region (v2 plugin). No pointers inside, offsets only.
- **Target latency**: taken from the sender's sync packets
  (`rtptime_now − rtptime_less_latency`, ≈ 77175 + 11025 frames ≈ 2.0 s for
  iOS), capped to the buffer. Playback for a session starts when the ring
  holds `target − reorderWindow` frames; `reorderWindow` defaults to 500 ms.
- **DriftController**: slow PI loop on the ring fill level (time constant
  ~10 s), output clamped to ±0.2 % around the nominal ratio
  `deviceRate / 44100`. libsamplerate interpolates ratio changes across each
  block, so ±0.01 % steps are inaudible. Overrun → ratio up slightly; if fill
  exceeds target + 1 s, drop to target (resync, with fade). Underrun → fade
  to silence over 5 ms, hold, fade back in when fill recovers to
  `target/2`.
- **Mixer**: float32 sum of all active sessions after per-source gain
  (RAOP volume −30…0 dB mapped to a curve, −144 = mute), mute, solo; master
  soft-clip only as a safety; per-source peak/RMS meters published as
  atomics.

Why buffer-level rather than NTP-locked playout: Tape In's job is to get a
stable stream into a DAW, not multi-room sync. Holding the buffer level
constant yields a constant latency and absorbs sender/receiver drift without
ever touching the audio thread with network timing. The NTP exchange is
still implemented (sender replies to our 0xD2 requests) and logged, so an
NTP-locked mode can be added later if multi-room sync is ever wanted.

---

## 3. Protocol summary (what the core must do for iOS to play)

Derived from shairport-sync, AirBass and the observed auxCord TXT record.

**Advertisement** (`_raop._tcp`, instance `<12 HEX>@<Name>`):
`txtvers=1 ch=2 cn=0,1 et=0,1 ek=1 sr=44100 ss=16 tp=UDP vn=3 da=true
sv=false md=0,1,2 pw=false sm=false am=TapeIn,1 vs=130.14`. This is
byte-for-byte the set auxCord advertises (plus `am`), which is known to work
with current iOS. The 12-hex prefix is **not** required to be a MAC: it is a
stable per-install random/hashed ID (auxCord uses one that is not the host's
MAC) and must match the 6 bytes used in the Apple-Response. The RTSP port is
dynamic (bind port 0, advertise the result).

**RTSP** on one TCP connection per sender:

| Request | What we do |
|---|---|
| any with `Apple-Challenge` | base64-decode (tolerate missing `=`); sign `challenge ‖ local IP (4/16) ‖ 6-byte ID`, zero-padded to ≥ 32 bytes, RSA PKCS#1 v1.5 **raw** (no DigestInfo); reply `Apple-Response` base64 without `=` |
| OPTIONS | `Public: ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, OPTIONS, GET_PARAMETER, SET_PARAMETER` |
| ANNOUNCE | parse SDP: `a=fmtp:` (12 ints, ALAC config), `a=rsaaeskey:` (RSA-OAEP-SHA1 → 16-byte AES key), `a=aesiv:` (16 bytes), `a=rtpmap:96 L16/44100/2` (PCM, used by pyatv), `a=min-latency`/`a=max-latency`; encryption iff both key and IV present; capture `DACP-ID`, `Active-Remote`, `User-Agent`, `X-Apple-Client-Name` |
| SETUP | parse `Transport` (`control_port`, `timing_port`); allocate 3 UDP sockets on the connection's local address; reply `Transport: RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;control_port=…;timing_port=…;server_port=…` and `Session: 1` |
| RECORD | start the stream; reply `Audio-Latency: 11025`; `RTP-Info rtptime` = initial flush point |
| FLUSH | drop buffered frames before `rtptime`; reset release/drift state |
| SET_PARAMETER | `text/parameters`: `volume: <dB>` / `progress: a/b/c`; `application/x-dmap-tagged`: metadata (bounded DMAP walk); `image/*`: artwork body |
| GET_PARAMETER | body `volume\r\n` → `volume: <dB>` |
| TEARDOWN | stop, close UDP sockets, `Connection: close` |

Always echo `CSeq`, send `Server: AirTunes/105.1` and `Content-Length`.
Unknown methods → 501; out-of-order (SETUP before ANNOUNCE) → 455/400.

**RTP** (three UDP sockets per session): audio 0x60 and resend-reply 0x56
(4-byte prefix before the inner header) on the server port; sync 0x54 and
resend-reply-on-control 0xD6 on the control port; timing 0xD2 request from
us / 0xD3 reply from sender on the timing port (first three at 300 ms, then
every 3 s). Every packet gets a minimum-length check before any field read
(12 for audio, 16 for resend, 20 for sync, 32 for timing).

**DACP** remote control: browse `_dacp._tcp` for `iTunes_Ctrl_<DACP-ID>`,
resolve host:port, send `GET /ctrl-int/1/<cmd>` with `Active-Remote`.
Commands for v1: `playpause`, `nextitem`, `previtem`, `volumeup`,
`volumedown`, `setproperty?dmcp.device-volume=<0..100>`. Expect the service
to disappear and reappear; re-resolve on demand. On macOS the browser has to
be written on `DNSServiceBrowse` (shairport-sync's dns_sd backend has none).

---

## 4. Platform interfaces

```cpp
namespace tapein::core::platform {

struct ServiceRecord { std::string instanceName, type; uint16_t port;
                       std::vector<std::pair<std::string,std::string>> txt; };

class IServiceAdvertiser {            // _raop._tcp registration
public:
  virtual ~IServiceAdvertiser() = default;
  virtual Result<void> publish(const ServiceRecord&) = 0;
  virtual Result<void> updateTxt(const ServiceRecord&) = 0;   // e.g. sf flags
  virtual void unpublish() = 0;
  // Called from the core reactor: file descriptor + process() for poll integration,
  // or nullopt if the backend uses its own thread and posts events.
  virtual std::optional<int> pollFd() const = 0;
  virtual void processEvents() = 0;
};

class IServiceBrowser {               // _dacp._tcp discovery
public:
  virtual ~IServiceBrowser() = default;
  virtual void browse(std::string_view type, BrowseCallback) = 0;
  virtual void resolve(std::string_view instance, std::string_view type,
                       uint32_t ifIndex, ResolveCallback) = 0;
  virtual std::optional<int> pollFd() const = 0;
  virtual void processEvents() = 0;
};

class ILoginItem {                    // start at login
public:
  enum class Status { notRegistered, enabled, requiresApproval, notFound };
  virtual Result<void> setEnabled(bool) = 0;
  virtual Status status() const = 0;
  virtual void openSystemSettings() = 0; // no-op on Windows
};

class IClock { virtual std::chrono::nanoseconds monotonic() const = 0; };
class ILogSink { virtual void write(Level, std::string_view) = 0; };
}
```

Crypto is **not** a platform interface in v1: Mbed TLS is portable and runs
inside the core, which lets the Apple-Challenge and key-unwrap unit tests run
on every CI OS. The three primitives are wrapped in small core classes
(`RsaRawSigner`, `RsaOaepDecryptor`, `AesCbcDecryptor`) so a
Security.framework or CNG backend can replace them later if binary size or
policy demands; the equivalence test (same challenge → same signature) is
part of the suite.

Sockets are a thin wrapper in `core/net` with one `#ifdef _WIN32` file
(`closesocket`, `WSAPoll`, error codes); the reactor is `poll()`-based on
both. This is the only platform-conditional code in the core, and it is
exercised by the Windows CI job.

Audio output is **not** a core interface either: the core offers a pull API
(`Mixer::render`) and the app connects it to JUCE's `AudioIODeviceCallback`.
This is what lets the same core feed a plugin's `processBlock` in v2.

---

## 5. Library choices and licenses

| Need | Choice | License | Status / notes |
|---|---|---|---|
| App framework | JUCE 9.0.4 | AGPLv3 option | 9.0.4 released 2026-10-12; 8.0.16-security same day (security-only). See Q1 |
| Crypto | Mbed TLS 3.6 LTS (FetchContent, minimal config: bignum, RSA, PK/PEM, AES, SHA-1, MD5) | Apache-2.0 OR GPL-2.0+ | 3.6 LTS supported to Mar 2027; 4.x splits crypto into TF-PSA-Crypto and is not yet worth the churn |
| ALAC decoder | Apple ALAC reference (`macosforge/alac`) | Apache-2.0 | Repo archived 2020, complete and portable; wrapped with fixed-size buffers and fuzzed. Alternative kept in mind: David Hammerton's decoder as carried by shairport-sync (MIT, ~1k lines) |
| Resampler | libsamplerate 0.2.2 (`SRC_SINC_BEST_QUALITY`, pull mode) | BSD-2 | `src_process` and the `*_vari_process` paths allocate nothing (verified in `src_sinc.c`; allocations only in `src_new`/`src_clone`). Fallback: zita-resampler `VResampler` (GPL-3) |
| mDNS macOS | `dns_sd.h` (system) | Apache-2.0 (mDNSResponder) | Use `DNSServiceRefSockFD` + `kDNSServiceFlagsShareConnection` in the reactor |
| mDNS Windows | `windns.h` DNS-SD (`DnsServiceRegister/Browse/Resolve`) | system | Windows 10 1809+; responder is the OS DNS Client, no extra firewall rule. Fallback: mjansson/mdns (public domain) |
| Unit tests | Catch2 v3 | BSL-1.0 | |
| Fuzzing | libFuzzer harnesses (clang) | — | RTSP/SDP/DMAP/RTP/ALAC parsers |
| Logging | tiny core logger → `ILogSink`; app writes a rotating file | — | No spdlog in core (keeps it dependency-light) |
| Integration sender | pyatv (`atvremote stream_file`) | MIT | See §10 |
| VST3 SDK (v2) | bundled by JUCE | **MIT** (3.8, Oct 2025) | Verified in the SDK's LICENSE.txt |
| ASIO SDK (Windows) | Steinberg ASIO SDK | GPLv3 OR proprietary (Oct 2025) | Verify file-level headers of the exact version when vendoring |

**Reference projects and what may be reused:**

| Project | License | Use |
|---|---|---|
| shairport-sync | MIT throughout the classic path (per-file headers) | Protocol reference; constants and algorithms may be ported with attribution. Clean-room by choice (C++20 design), not by necessity |
| AirBass (+2021 fork) | Apache-2.0 | Closest macOS-native analogue; DACP client and Security.framework usage as reference |
| UxPlay | LGPL-2.1+ core, GPLv3+ rest | Not a RAOP reference (no RSA path). Reusable: Windows `dnssd.dll` runtime-load pattern, Windows firewall runbook |
| airplay2-receiver | **no license** (all rights reserved); two files GPLv2-only | Read for understanding only (feature-bit table, sender behaviour notes). Nothing copied |

Every vendored dependency gets a directory under `third_party/` with its
LICENSE, a pinned upstream commit, and an entry in `THIRD_PARTY.md`, which is
also shipped inside the app bundle (About window).

---

## 6. Minimum macOS version: 13.0 (Ventura)

- **API floor**: `SMAppService` (start at login) is macOS 13+. Everything
  else v1 needs — `dns_sd`, CoreAudio via JUCE, `SecKey*` — is 10.12+.
- **Nothing in v1 needs 14**. Local Network privacy (15+) is runtime
  behaviour, not a deployment-target constraint: on 15+ the first
  `DNSServiceRegister` triggers the prompt, so the app ships
  `NSLocalNetworkUsageDescription` and `NSBonjourServices`
  (`_raop._tcp`, `_dacp._tcp`) and handles `kDNSServiceErr_PolicyDenied`
  regardless of the chosen floor.
- **Toolchain**: Xcode 16/26 SDKs accept `CMAKE_OSX_DEPLOYMENT_TARGET=13.0`;
  JUCE 9's own floor is 10.11.
- **Costs**: Ventura stopped receiving security updates in 2025 and is a
  small share of active Macs (TelemetryDeck's mid-2026 sample does not list
  it in the top ten). CI cannot run 13, so it is "best effort": we do not
  break it knowingly, but the tested tier is 14+ (and the maintainer's 26).
- **Why not 14**: the "turn an old Mac into an AirPlay speaker for the
  studio" use case lands on exactly the Intel machines stuck on 13. Raising
  the floor buys us nothing technical today. Revisit if a 14-only API is
  needed. (auxCord declares 10.13.)

Universal binary: `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`; tests run
natively on arm64 and under Rosetta for the x86_64 slice.

---

## 7. How v2 (plugin) fits

- The plugin target (`juce_add_plugin`, formats AU + VST3, same repo) links
  only `core/dsp` and `core/buffer` — never `core/net`, `core/rtsp`,
  `platform/`. It maps the SpscRing from shared memory
  (`shm_open`/`mmap` on macOS, `CreateFileMapping` on Windows), runs the same
  Resampler + DriftController in `processBlock`, and shows "Tape In app not
  running" when the ring's heartbeat is stale.
- The standalone app becomes the **broker**: it owns all sockets, Bonjour,
  permissions, and one receiver per plugin instance (each instance = its own
  `_raop._tcp` name, chosen in the plugin UI and persisted in the DAW
  project). The engine already supports many receivers; v1 only exposes one
  in the UI.
- Why no networking in the DAW: on macOS 15+ the Local Network prompt is
  attributed to the *responsible process* — the DAW — and `NSBonjourServices`
  must be in the DAW's Info.plist, which we do not control; the Application
  Firewall likewise prompts in the DAW's name; sandboxed hosts (GarageBand,
  App Store Logic) block raw sockets entirely. *(Observed: auxCord's binary
  contains both a `PluginProcessor` and the RTSP/DACP classes in one JUCE
  project, so its plugin likely runs networking in-process; see Q4.)*
- Known v2 gotchas the v1 design already accounts for: sample rate and
  buffer size are properties of the consumer (resampler lives on the reader
  side); the ring header carries a generation counter + heartbeat so a
  restarted app or dead plugin is detected; readers tolerate variable block
  sizes. Sandboxed hosts cannot open our shm name, so a loopback-UDP
  transport is the planned fallback (same ring semantics over datagrams).
  Prior art: discoLink (MIT, 2026), Blue Cat Connector, ReaStream.

---

## 8. How the Windows port fits, and its specific risks

| Area | Plan | Risk |
|---|---|---|
| mDNS | `windns.h` DNS-SD behind `IServiceAdvertiser`/`IServiceBrowser` | API only on Windows 10 1809+; disabled when a Hyper-V virtual switch exists *(reported 2015, re-verify)*; Browse returns names only → always Resolve. Fallback: mjansson/mdns (needs our own UDP 5353 socket + inbound firewall rule) |
| Firewall | Installer adds inbound rules for the RTSP TCP port range and RTP UDP range on the Private profile; first-run prompt otherwise | Users on "Public" profile networks see nothing; document it (UxPlay's README is the model) |
| Audio | WASAPI shared + exclusive via JUCE; ASIO via JUCE with the GPLv3 ASIO SDK vendored | ASIO SDK dual license is new (Oct 2025) — confirm headers; "ASIO" is a Steinberg trademark, descriptive use only |
| Crypto / codec / resampler | Same Mbed TLS / ALAC / libsamplerate, MSVC + clang-cl | None expected; CI builds the core on `windows-2025` from Phase 1 |
| Sender for testing | Apple Music/iTunes for Windows speak classic RAOP | Good: a native AirPlay-1 sender exists on Windows |
| Login | `HKCU\...\Run` with `asInvoker` manifest; read `StartupApproved\Run` to reflect user toggles | None |
| Tray | JUCE `SystemTrayIconComponent` | None |
| Installer | Signed MSI/Inno; code-signing cert (EV or OV) needed | Cost/time; SmartScreen reputation |

---

## 9. Legal and IP questions (for the maintainer to decide)

1. **The RAOP RSA private key.** Every open-source AirPlay-1 receiver
   (shairport-sync `common.c`, AirBass `PrivateKey.pem`, airplay2-receiver
   `airplay1.py`, OwnTone, and auxCord *(inferred: it links OpenSSL and
   answers Apple-Challenge)*) embeds the same 2048-bit private key, obtained
   from AirPort Express firmware (public since James Laird's 2011 shairport
   release). It cannot be avoided: iOS sends `Apple-Challenge` and the
   receiver must sign it regardless of whether audio encryption (`et=1`) is
   used; shairport-sync answers it on every request and has no path that
   succeeds without the key. None of the projects comment on provenance or
   legality in their trees. Tape In needs the key to work with iOS. Options:
   (a) embed it like everyone else, with a clear note in `docs/LEGAL.md`;
   (b) load it from a user-supplied file at runtime (still needs the user to
   obtain it; worse UX, same key). Recommendation: (a). **Decision needed.**
2. **"AirPlay" trademark.** Product name contains no "AirPlay"; descriptive
   use ("works with AirPlay") plus the no-affiliation notice in README,
   About window and DMG. We are not in the MFi program and must not use the
   "Works with Apple AirPlay" badge.
3. **AGPL §13.** An iPhone streaming to Tape In is arguably "interacting
   through a network". Cheap, defensible compliance: source URL in the About
   window, in the RTSP `Server` header comment, and as a `src=` TXT key.
   Proposed; harmless if unnecessary.
4. **Licenses of reference code.** shairport-sync (MIT) and AirBass
   (Apache-2.0) may be ported with attribution; UxPlay only as GPLv3-marked
   files; airplay2-receiver must not be copied (no license; two GPLv2-only
   files). Policy: write the core fresh; if a routine is ported, keep the
   original header and list it in `THIRD_PARTY.md`.
5. **JUCE AGPL option.** No splash or attribution requirement in JUCE 8/9.
   The DMG ships the AGPL text and a source offer (link to the tagged
   release). A future closed-source fork would need a commercial JUCE
   license — irrelevant for us.

---

## 10. Testing strategy

- **Unit (core, every CI OS)**: SpscRing (single-thread + TSan stress),
  JitterBuffer (wrap-around, late/duplicate/resend), DriftController (step
  and ramp responses), Resampler (ratio sweep, no clicks via spectral
  check), RTSP parser (valid + ~40 malformed cases from §11 of the
  shairport-sync study), SDP/fmtp, DMAP walker, RTP packet parsers, ALAC
  wrapper (known vectors + truncated/garbage input), crypto (challenge
  vectors captured from a real iPhone session; OAEP unwrap vector).
- **Fuzz (clang CI job)**: libFuzzer harnesses for every parser with a
  corpus seeded from captured sessions.
- **Integration (macOS CI)**: pyatv streams a known WAV to the CLI receiver
  on localhost; compare decoded output sample-by-sample after aligning the
  leading silence. pyatv's RAOP sender is MIT and supports classic RAOP with
  RSA/AES *(details in the Phase-1 report once exercised)*.
- **Manual matrix (maintainer)**: iPhone (Spotify, Apple Music, YouTube,
  Voice Memos), another Mac (system audio and DAW output), Windows laptop
  (Apple Music/iTunes for Windows), sinks BlackHole + Focusrite loopback into
  Ableton Live; sleep/wake, Wi‑Fi switch, output switch, sample-rate change.
- **Compatibility probe (Phase 1, first thing)**: an iOS 26 iPhone picks the
  CLI receiver in Control Center and audio arrives. This is the empirical
  answer to "does current iOS still talk AirPlay 1"; the strong prior is
  yes, because auxCord (RAOP-only) is in daily use on this machine.

---

## 11. Latency

The ~2 s is sender-imposed: iOS sends audio ahead by the amount announced
in its sync packets (≈ 88200 frames incl. the 11025 "fixed latency" bucket)
and expects the receiver to play at that offset. A receiver can play
*earlier* than the sender's timestamps (shairport-sync's
`audio_backend_latency_offset`) but then has less protection against loss
and resends, and FLUSH/seek behaviour becomes visibly off. Tape In v1 keeps
the sender's latency and exposes an advanced "latency offset" setting
(default 0, range −1.5 s…+0.5 s) in Phase 2 for users who prefer earlier
playback over robustness. Real reductions require AirPlay 2's buffered mode
or a different protocol; both are out of scope. auxCord documents the same
limit.

---

## 12. Risks, ordered by impact

| # | Risk | Impact | Likelihood | Mitigation |
|---|---|---|---|---|
| 1 | Apple drops AirPlay 1 from iOS/macOS senders | Fatal for the product | Low in 12 months (auxCord RAOP-only is in daily use on iOS 26-era devices; Apple Music for Windows still needs classic) | Phase-1 probe first; keep the core protocol-agnostic enough that an AirPlay 2 receiver could be added (HomeKit pairing + buffered audio) as a later major effort |
| 2 | RSA key legal exposure | Project takedown | Low (15 years of public use, no known action) | Maintainer decision (§9.1); document provenance; be ready to move the key to a user-supplied file |
| 3 | Local Network permission UX on macOS 15+ | Users think the app is broken | High (every install) | Usage strings, in-app detection of `PolicyDenied`, guided link to System Settings, README section |
| 4 | iOS behaviour with many concurrent sessions is undocumented | Multi-source feature may need redesign | Medium | Design per *client* (DACP-ID) with multiple RTSP connections, as auxCord does; test with 2–3 iPhones in Phase 2 |
| 5 | Drift/resampling quality (clicks, slow convergence) | Core UX | Medium | Buffer-level PI control with clamped slew; unit-tested step responses; long-run soak tests |
| 6 | JUCE 9's new CoreAudio layer has early bugs | Device switching / sample-rate changes | Medium | Pin 9.0.4; core is JUCE-free so dropping to 8.0.16 is a CMake change; test USB DAC 44.1/48/96 switching early |
| 7 | Sleep/wake and network changes leave sockets/mDNS stale | Robustness criterion | Medium | Re-register Bonjour and rebind listener on interface change (SCNetworkReachability / `NWPathMonitor` via a small Obj-C++ shim), session timeout via TCP keepalive ≈ 60 s + audio silence timeout |
| 8 | Windows mDNS API limitations / Hyper-V | Windows port slips | Medium | Fallback mdns library already selected; smoke test on a VM before committing |
| 9 | Notarization/Gatekeeper and SMAppService signing rules | Distribution delays | Low | Developer ID + hardened runtime + `notarytool` pipeline in Phase 4; note: `notarytool` needs Xcode, which is not installed on the maintainer's Mac |
| 10 | 2 s latency is not reducible | Users expecting live monitoring | Certain | Document; offset setting; roadmap note on AirPlay 2 |
| 11 | ASIO SDK relicensing details | Windows ASIO support | Low | WASAPI exclusive is the baseline; ASIO only after reading the vendored SDK's headers |

---

## 13. Questions for the maintainer

Ordered by how much the answer changes the work.

1. **JUCE 9 instead of JUCE 8?** JUCE 9.0.4 is current; 8 is security-only
   since July 2026. I recommend 9. If you prefer 8 for stability, say so
   and we pin 8.0.16.
2. **Embed the RAOP RSA key** (§9.1, option a)? Without it iOS will not
   stream. This is your call; the code will be structured so the key lives
   in one file either way.
3. **Minimum macOS 13.0** as proposed (§6)? Alternative is 14.0 with no
   technical gain today.
4. **auxCord's plugin.** Does the auxCord you own include an AU/VST3, and
   when you use it inside Ableton/Logic, does the DAW (not auxCord) ask for
   Local Network / firewall permission? That tells us whether Mountainroad
   put networking inside the plugin, which calibrates the v2 broker design.
5. **Which iOS version is on the iPhone you use with auxCord?** That is our
   current empirical "AirPlay 1 still works" data point; the Phase-1 probe
   will confirm it with our own receiver.
6. **Session policy.** auxCord says "up to 100 sources". Do you want a hard
   cap (configurable, default 100) or unlimited? Default proposal: cap 100.
7. **Default receiver name.** "Tape In" (so several Macs show "Tape In",
   "Tape In (2)"…) or "Tape In on <hostname>"? Proposal: "Tape In".
8. **AGPL §13 `src=` TXT key and source link in the About window** — OK to
   add?
9. **Windows test sender.** Do you have Apple Music or iTunes for Windows
   on the laptop? If not, which AirPlay sender do you use there?
10. **Xcode.** Phase 4 (notarization) needs full Xcode for `notarytool` and
    `stapler`; the Mac currently has only the Command Line Tools. Installing
    it is on you (it is a system component); nothing in Phases 1–3 needs it.
11. **Password-protected receiver (`pw=true`)** — in scope for v1 or not?
    Proposal: not in v1 (auxCord advertises `pw=false`).

---

## 14. What Phase 1 will build (preview, for the OK)

`tools/tapein-cli --name "Tape In Dev" --out capture.wav`: advertises one
receiver on a dynamic port, accepts one or more senders, logs the RTSP
dialogue, decodes to 44.1 kHz/16-bit stereo and writes each session to its
own WAV (no resampling yet, no audio device). Plus: CMake skeleton, core
library with parsers + crypto + ALAC + jitter buffer, Catch2 suite, fuzz
harnesses, GitHub Actions matrix (macOS ASan/UBSan, Linux ASan/UBSan/TSan,
Windows MSVC ASan), and the pyatv integration test. Acceptance criteria as
in SPEC.md §6 Phase 1.
