# Phase 3 — Remote Control & Now-Playing Metadata Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The app shows what each source is playing (track, artist, album, artwork, progress) and can drive the sender back (play/pause, next, previous, volume), presented through the approved neumorphic vertical UI.

**Architecture:** Metadata arrives *pushed* from the sender over RTSP `SET_PARAMETER` (DMAP blob, JPEG/PNG artwork, `progress:` line); it is parsed by a pure, fuzzable core parser, routed `Receiver → Session → IPcmSink → Engine`, cached on the `Source`, and surfaced on `SourceSnapshot`. Remote control goes the other way: the Engine resolves the sender's DACP server (`iTunes_Ctrl_<DACP-ID>` on `_dacp._tcp`) via a new `IServiceResolver`, then sends a one-shot HTTP `GET /ctrl-int/1/<cmd>` with the captured `Active-Remote` token over `net::TcpSocket` — all on the reactor thread. The JUCE app layer is rewritten to the neumorphic design (Boldonse brand font embedded as BinaryData, soft-shadow look-and-feel, vertical slot list).

**Tech Stack:** C++20, CMake/Ninja, Catch2, libFuzzer (Linux clang CI), dns_sd (macOS), JUCE 9.0.4 (app only).

**Spec:** `docs/SPEC.md` §6 (Phase 3); approved UI design in artifact "Tape In — Phase 3 UI (Neumorphism)"; integration map established in-session (see each task's Files).

## Global Constraints

- C++20; CMake ≥ 3.25; Ninja. Universal app: `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`; min macOS 13.0.
- `core/` and `platform/` **never** include a JUCE header; Objective-C++ (`.mm`) only under `platform/macos/` and `app/`.
- Every byte from the network is hostile: explicit length checks before every field read; bounded sizes; parsers are pure functions over `std::span<const uint8_t>`/`std::string_view`, unit-tested and fuzzed.
- Audio-thread rules unchanged: metadata/remote-control code runs on the reactor or UI thread, never the audio thread; no locks/alloc/IO on the audio callback.
- No exceptions across the core API boundary; use `Result<T>`. No `using namespace` in headers. Prefer `std::span`, `std::string_view`.
- Every new source file starts with the two SPDX header lines. Code/identifiers/comments/commits in English.
- Do not use "AirPlay" in product names/bundle IDs. Bundle ID stays `app.tapein.mac`.
- Ask before: an AGPL-incompatible dependency, anything needing `sudo`/system components, or the maintainer's Apple Developer account. (This phase needs none.)
- Boldonse font is OFL-1.1, embedded as BinaryData; already vendored at `app/Resources/fonts/` and listed in `THIRD_PARTY.md`.
- One branch per phase (`phase-3/remote-metadata`), small descriptive commits.

## Review Focus

1. **Truncated / lying DMAP lengths** — a tag whose declared length runs past the buffer end must never read out of bounds; the parser returns what it safely got. *(Task 1: explicit truncated-length test + fuzz corpus.)*
2. **Non-UTF-8 / overlong metadata strings** — a hostile `minm` is clamped to a bounded UTF-8 string without splitting a code point; the UI never receives unbounded/garbage text. *(Task 1: invalid-UTF-8 + oversize test.)*
3. **Degenerate artwork** — zero-byte, oversize (near the 1 MB `Content-Length` cap), or undecodable image: bounded copy in core, placeholder in the UI, no crash. *(Task 3 bound test; Task 10 placeholder-on-decode-failure.)*
4. **Sender unreachable when a command is sent** — resolve miss, connect refused, or connect that never completes: `sendTransportCommand` fails silently and the reactor stays responsive (no hang, no crash). *(Task 7: refused-connect test; Task 8: unresolvable-source test.)*
5. **Command for a disconnected/lingering source** — a source with no `Active-Remote`, or removed between UI click and reactor run, is a guarded no-op. *(Task 8: command-after-end test.)*

---

## Task 1: DMAP metadata & progress parsers (core, pure + fuzzed)

**Files:**
- Create: `core/include/tapein/core/session/dmap.h`, `core/src/session/dmap.cpp`
- Test: `tests/unit/dmap_test.cpp`; register in `tests/CMakeLists.txt` (add to `tapein_tests` sources)
- Create: `tests/fuzz/dmap_fuzz.cpp`; register in `tests/fuzz/CMakeLists.txt`; seed corpus dir `tests/fuzz/corpus/dmap/`
- Modify: `core/CMakeLists.txt` (add `src/session/dmap.cpp` to the core library sources)

**Interfaces:**
- Produces:
  - `struct DmapMetadata { std::string trackName; std::string artist; std::string album; };`
  - `struct DmapProgress { uint32_t startFrames = 0; uint32_t currentFrames = 0; uint32_t endFrames = 0; };`
  - `DmapMetadata parseDmapMetadata(std::span<const uint8_t> body);` — iterates top-level DMAP tags (`code[4]` + `uint32` big-endian length + payload), recursing one level into the container codes `mlit`/`mlcl`/`mshl`; captures `minm`→trackName, `asar`→artist, `asal`→album as UTF-8 **clamped to 256 bytes via `clampUtf8`**; unknown/oversized/truncated tags are skipped safely.
  - `std::optional<DmapProgress> parseProgress(std::string_view line);` — parses `"progress: <start>/<cur>/<end>"` (also accepts the three numbers without the prefix); returns `nullopt` if not three unsigned integers.

- [ ] **Step 1: Write the failing tests** in `tests/unit/dmap_test.cpp`

```cpp
// Helper in the test: append a DMAP tag (4-char code, u32be length, bytes).
TEST_CASE("parseDmapMetadata reads minm/asar/asal from a flat blob") {
    auto body = tag("minm", "Midnight City") + tag("asar", "M83") + tag("asal", "Hurry Up, We're Dreaming");
    auto md = parseDmapMetadata(asSpan(body));
    CHECK(md.trackName == "Midnight City");
    CHECK(md.artist == "M83");
    CHECK(md.album == "Hurry Up, We're Dreaming");
}
TEST_CASE("parseDmapMetadata recurses one level into mlit") {
    auto inner = tag("minm", "Weightless") + tag("asar", "Marconi Union");
    auto body = tag("mlit", inner);
    auto md = parseDmapMetadata(asSpan(body));
    CHECK(md.trackName == "Weightless");
    CHECK(md.artist == "Marconi Union");
}
TEST_CASE("parseDmapMetadata ignores a tag whose length overruns the buffer") {
    std::vector<uint8_t> body = {'m','i','n','m', 0x00,0x00,0x00,0x40, 'h','i'}; // declares 64, has 2
    auto md = parseDmapMetadata(asSpan(body)); // must not read past end
    CHECK(md.trackName.empty());
}
TEST_CASE("parseDmapMetadata clamps an overlong name to 256 bytes on a code-point boundary") {
    std::string big(1000, 'a');
    auto md = parseDmapMetadata(asSpan(tag("minm", big)));
    CHECK(md.trackName.size() <= 256);
}
TEST_CASE("parseProgress returns the three frame counts") {
    auto p = parseProgress("progress: 1/44101/176400");
    REQUIRE(p.has_value());
    CHECK(p->startFrames == 1); CHECK(p->currentFrames == 44101); CHECK(p->endFrames == 176400);
}
TEST_CASE("parseProgress rejects a malformed line") {
    CHECK_FALSE(parseProgress("progress: 1/2").has_value());
}
```

- [ ] **Step 2: Run to verify they fail** — `ctest --test-dir build -R dmap` → FAIL (undeclared `parseDmapMetadata`).
- [ ] **Step 3: Implement** `parseDmapMetadata` and `parseProgress` in `core/src/session/dmap.cpp` per the Interfaces contract. Big-endian length via existing `util/byte_reader.h` if convenient; reuse `session::clampUtf8` (declared in `engine.h` — move its declaration to `util/` is out of scope, so `#include` and call it, or re-declare a local `clampUtf8` in `util`; **Ruling point:** if a cross-include is awkward, add `std::string clampUtf8(std::string_view, size_t)` to `core/util/` and have `engine.cpp` use that — pick the smaller diff).
- [ ] **Step 4: Run to verify they pass** — `ctest --test-dir build -R dmap` → PASS.
- [ ] **Step 5: Write the fuzz harness** `tests/fuzz/dmap_fuzz.cpp` (replays into `parseDmapMetadata`), register it, add 2–3 seeds to `tests/fuzz/corpus/dmap/` (a valid blob, a truncated-length blob, an all-container blob). Mirror `rtsp_parser_fuzz.cpp`.
- [ ] **Step 6: Build + run the corpus replay** — `ctest --test-dir build -R dmap_fuzz` → PASS.
- [ ] **Step 7: Commit** — `git add` the new files + CMake edits; `git commit -m "core: DMAP metadata and progress parsers (pure, fuzzed)"`.

---

## Task 2: Sink callbacks + Receiver/Session routing (core)

**Files:**
- Modify: `core/include/tapein/core/session/pcm_sink.h` (new structs are `DmapMetadata`/`DmapProgress` from Task 1 via include; add virtuals)
- Modify: `core/include/tapein/core/session/session.h`, `core/src/session/session.cpp` (new forwarders)
- Modify: `core/src/session/receiver.cpp` (`handleSetParameter`, ~lines 518-546)
- Test: `tests/unit/receiver_test.cpp` (extend)

**Interfaces:**
- Consumes: `DmapMetadata`, `DmapProgress`, `parseDmapMetadata`, `parseProgress` (Task 1).
- Produces (new **default-empty** `IPcmSink` virtuals, matching the `onSenderLatency` pattern):
  - `virtual void onMetadata(uint64_t sessionId, const DmapMetadata&) {}`
  - `virtual void onArtwork(uint64_t sessionId, std::span<const uint8_t> image, std::string_view mime) {}`
  - `virtual void onProgress(uint64_t sessionId, const DmapProgress&) {}`
  - `Session` forwarders mirroring `setVolume`: `void setMetadata(const DmapMetadata&)`, `void setArtwork(std::span<const uint8_t>, std::string_view mime)`, `void setProgress(const DmapProgress&)` — each calls the matching `m_sink.on*` with `m_info.id`.

- [ ] **Step 1: Write the failing test** in `receiver_test.cpp`: a recording `IPcmSink` subclass; drive a SET_PARAMETER through the existing receiver-test harness with `Content-Type: application/x-dmap-tagged` and a `minm` blob; assert the sink's recorded metadata `trackName`. Add a second case: `Content-Type: image/jpeg` with 3 bytes → `onArtwork` gets 3 bytes and mime `"image/jpeg"`. Third: `text/parameters` body `progress: 0/100/200` → `onProgress` gets `{0,100,200}`.
- [ ] **Step 2: Run to verify it fails** — `ctest --test-dir build -R receiver` → FAIL.
- [ ] **Step 3: Implement** — add the virtuals to `pcm_sink.h`; add the `Session` forwarders; in `Receiver::handleSetParameter` add sibling branches: `application/x-dmap-tagged` → `session->setMetadata(parseDmapMetadata(bodyView(request)))`; `image/` prefix → `session->setArtwork(request.body, contentType)`; inside the existing `text/parameters` loop add `else if (line.starts_with("progress:")) if (auto p = parseProgress(line)) session->setProgress(*p);`. Keep the `200` response. Guard `session != nullptr`.
- [ ] **Step 4: Run to verify it passes** — `ctest --test-dir build -R receiver` → PASS.
- [ ] **Step 5: Run the whole suite** — `ctest --test-dir build` → all PASS (no existing sink broke, thanks to default-empty virtuals).
- [ ] **Step 6: Commit** — `git commit -m "core: route SET_PARAMETER metadata/artwork/progress to the sink"`.

---

## Task 3: Engine metadata cache + snapshot fields (core)

**Files:**
- Modify: `core/include/tapein/core/session/engine.h` (`Source` struct, `SourceSnapshot` struct, `IPcmSink` overrides), `core/src/session/engine.cpp`
- Test: `tests/unit/engine_test.cpp` (extend)

**Interfaces:**
- Consumes: Task 2 virtuals; `DmapMetadata`, `DmapProgress`.
- Produces — new `SourceSnapshot` fields: `std::string trackName, artist, album; bool hasArtwork=false; std::vector<uint8_t> artwork; std::string artworkMime; uint32_t elapsedSeconds=0; uint32_t durationSeconds=0;`. New `Source` cache fields mirror these (`DmapMetadata metadata; std::vector<uint8_t> artwork; std::string artworkMime; DmapProgress progress;`). Engine overrides `onMetadata/onArtwork/onProgress` (reactor thread): store under `m_mutex`, then `emit({sourceChanged, sourceId})`. Artwork copy is capped at 1 MB (drop if larger). `elapsedSeconds = (current-start)/44100` clamped ≥0; `durationSeconds = (end-start)/44100`.

- [ ] **Step 1: Write the failing test** in `engine_test.cpp`: using the existing owned-/external-reactor engine harness, deliver metadata + progress for a live session (call the Engine's `IPcmSink` overrides directly, as other engine tests do), then `sources()` and assert `trackName`, `artist`, `elapsedSeconds==1`, `durationSeconds>=3`, and that a `sourceChanged` event was emitted.
- [ ] **Step 2: Run to verify it fails** — `ctest --test-dir build -R engine` → FAIL (no `trackName` field).
- [ ] **Step 3: Implement** the `Source`/`SourceSnapshot` fields and the three overrides in `engine.cpp`; copy the cache into the snapshot in `sources()` (under `m_mutex`, beside the existing `s.dacpId = ...` copies).
- [ ] **Step 4: Run to verify it passes** — `ctest --test-dir build -R engine` → PASS.
- [ ] **Step 5: Run the whole suite** — `ctest --test-dir build` → all PASS.
- [ ] **Step 6: Commit** — `git commit -m "core: cache now-playing metadata on the source and expose it on the snapshot"`.

---

## Task 4: IServiceResolver interface + null implementation (core/platform)

**Files:**
- Create: `core/include/tapein/core/platform/service_resolver.h`
- Create: `platform/include/tapein/platform/null_resolver.h`, `platform/null/null_resolver.cpp`; register in `platform/CMakeLists.txt`
- Test: `tests/unit/service_resolver_test.cpp` (register in `tests/CMakeLists.txt`)

**Interfaces:**
- Produces:
  - `struct ResolvedService { std::string instanceName; std::string hostTarget; net::Endpoint endpoint; };`
  - ```cpp
    class IServiceResolver {
    public:
      virtual ~IServiceResolver() = default;
      using Callback = std::function<void(Result<ResolvedService>)>;
      virtual void resolve(std::string instanceName, std::string type, Callback cb) = 0;
      [[nodiscard]] virtual std::optional<net::NativeHandle> pollHandle() const = 0;
      virtual void processEvents() = 0;
      virtual void cancel() = 0;
    };
    ```
  - `NullServiceResolver` — a `resolve` that stores the callback and fires it with a configurable `Result<ResolvedService>` when `processEvents()` is called (or immediately if constructed in immediate mode); `pollHandle()` returns `nullopt`. A test constructor takes the canned result.

- [ ] **Step 1: Write the failing test** — a `NullServiceResolver` seeded with `ResolvedService{"iTunes_Ctrl_X","host.local",{ "127.0.0.1", 3689 }}`; call `resolve(...)`, assert the callback receives that endpoint; a second instance seeded with an error `Result` delivers the error.
- [ ] **Step 2: Run to verify it fails** — `ctest --test-dir build -R service_resolver` → FAIL.
- [ ] **Step 3: Implement** the interface header and `NullServiceResolver`.
- [ ] **Step 4: Run to verify it passes** — PASS.
- [ ] **Step 5: Commit** — `git commit -m "platform: add IServiceResolver with a null implementation"`.

---

## Task 5: macOS DNS-SD resolver (platform, macOS)

**Files:**
- Create: `platform/include/tapein/platform/dns_sd_resolver.h`, `platform/macos/dns_sd_resolver.cpp`; register in `platform/CMakeLists.txt` (APPLE only, beside `dns_sd_advertiser.cpp`)
- Test: `tests/unit/dns_sd_resolver_test.cpp` (register; guard body with `#if defined(__APPLE__)`)

**Interfaces:**
- Consumes: `IServiceResolver` (Task 4), `net::Reactor`, existing `DnsSdAdvertiser` (for the test).
- Produces: `DnsSdResolver : IServiceResolver` — `resolve` starts `DNSServiceResolve` for `<instance>.<type>.local`, then on its reply chains `DNSServiceGetAddrInfo` to get an IP, then invokes the callback with host+`Endpoint`; `pollHandle()`/`processEvents()` drive the underlying `DNSServiceRef` fd (mirror `DnsSdAdvertiser`).

- [ ] **Step 1: Write the failing test** (macOS): advertise a `_dacp._tcp` service instance `iTunes_Ctrl_TEST123` on port 50999 via `DnsSdAdvertiser` on a reactor; use `DnsSdResolver` to `resolve("iTunes_Ctrl_TEST123","_dacp._tcp")`, pump the reactor until the callback fires (bounded, e.g. 5 s), assert `endpoint.port == 50999`.
- [ ] **Step 2: Run to verify it fails** — `ctest --test-dir build -R dns_sd_resolver` → FAIL (class missing).
- [ ] **Step 3: Implement** `DnsSdResolver` with `DNSServiceResolve` + `DNSServiceGetAddrInfo`, registering the service ref fd via `pollHandle()`.
- [ ] **Step 4: Run to verify it passes** (macOS) — PASS. On Linux the guarded test is a no-op; the suite stays green.
- [ ] **Step 5: Commit** — `git commit -m "platform: macOS DNS-SD resolver for the sender's DACP service"`.

---

## Task 6: DACP request builder (core, pure)

**Files:**
- Create: `core/include/tapein/core/session/dacp_client.h` (declarations only this task), `core/src/session/dacp_client.cpp`; add to `core/CMakeLists.txt`
- Test: `tests/unit/dacp_client_test.cpp` (register)

**Interfaces:**
- Produces:
  - `enum class TransportCommand { playPause, next, previous };`
  - `std::string dacpPath(TransportCommand cmd);` → `"/ctrl-int/1/playpause"`, `"/ctrl-int/1/nextitem"`, `"/ctrl-int/1/previtem"`.
  - `std::string dacpVolumePath(float dB);` → `"/ctrl-int/1/setproperty?dmcp.device-volume=" + fmt`, dB clamped to `[-30, 0]`, formatted `"%.4f"`.
  - `std::string buildDacpGet(std::string_view path, std::string_view host, uint16_t port, std::string_view activeRemote);` → an HTTP/1.1 GET with `Host`, `Active-Remote`, `Connection: close`, CRLF terminated.

- [ ] **Step 1: Write the failing tests**

```cpp
CHECK(dacpPath(TransportCommand::playPause) == "/ctrl-int/1/playpause");
CHECK(dacpPath(TransportCommand::next)      == "/ctrl-int/1/nextitem");
CHECK(dacpPath(TransportCommand::previous)  == "/ctrl-int/1/previtem");
CHECK(dacpVolumePath(-12.0f) == "/ctrl-int/1/setproperty?dmcp.device-volume=-12.0000");
CHECK(buildDacpGet("/ctrl-int/1/playpause", "192.168.1.5", 3689, "cafebabe") ==
  "GET /ctrl-int/1/playpause HTTP/1.1\r\n"
  "Host: 192.168.1.5:3689\r\n"
  "Active-Remote: cafebabe\r\n"
  "Connection: close\r\n\r\n");
```

- [ ] **Step 2: Run to verify it fails** — `ctest --test-dir build -R dacp` → FAIL.
- [ ] **Step 3: Implement** the three free functions in `dacp_client.cpp`.
- [ ] **Step 4: Run to verify it passes** — PASS.
- [ ] **Step 5: Commit** — `git commit -m "core: DACP request builder (transport + volume paths)"`.

---

## Task 7: DacpClient — resolve + connect + send (core)

**Files:**
- Modify: `core/include/tapein/core/session/dacp_client.h`, `core/src/session/dacp_client.cpp`
- Test: `tests/unit/dacp_client_test.cpp` (extend)

**Interfaces:**
- Consumes: Task 6 builders; `IServiceResolver` (Task 4); `net::Reactor`, `net::TcpSocket`.
- Produces:
  - ```cpp
    class DacpClient {
    public:
      DacpClient(net::Reactor& reactor, platform::IServiceResolver& resolver);
      // Fire-and-forget on the reactor thread. dacpId/activeRemote from the Source.
      void send(std::string dacpId, std::string activeRemote, std::string path);
    };
    ```
  - Resolves `"iTunes_Ctrl_" + dacpId` on `"_dacp._tcp"`; on success connects a `TcpSocket` to the endpoint, writes `buildDacpGet(path, host, port, activeRemote)`, and closes (response is read-and-discarded or ignored). Resolve failure / connect failure is logged and dropped — never throws, never blocks the reactor.

- [ ] **Step 1: Write the failing test**: a `net::TcpListener` bound on `127.0.0.1:0` registered on a real `Reactor` that accepts one connection and reads the request bytes into a buffer; a `NullServiceResolver` seeded to resolve to that listener's port; construct `DacpClient`, call `send("ABC","cafebabe","/ctrl-int/1/playpause")`, pump the reactor until the request arrives, assert the captured bytes equal `buildDacpGet("/ctrl-int/1/playpause","127.0.0.1",<port>,"cafebabe")`.
- [ ] **Step 2: Run to verify it fails** — FAIL.
- [ ] **Step 3: Implement** `DacpClient::send` (resolve → connect → write → close).
- [ ] **Step 4: Run to verify it passes** — PASS.
- [ ] **Step 5: Write the failing test for the unreachable case**: seed the resolver to succeed with a port where nothing listens (connect refused); `send(...)`; pump the reactor briefly; assert no crash and the reactor still runs a subsequently-posted callback. (Covers Review Focus #4.)
- [ ] **Step 6: Run to verify it passes** — PASS.
- [ ] **Step 7: Commit** — `git commit -m "core: DacpClient — resolve, connect and send one-shot DACP commands"`.

---

## Task 8: Engine remote-control API (core)

**Files:**
- Modify: `core/include/tapein/core/session/engine.h`, `core/src/session/engine.cpp`
- Modify: `core/src/session/engine.cpp` (Engine now owns a `DacpClient` + an injected `IServiceResolver`)
- Test: `tests/unit/engine_test.cpp` (extend)

**Interfaces:**
- Consumes: `DacpClient` (Task 7), `IServiceResolver` (Task 4), `TransportCommand`/`dacpPath`/`dacpVolumePath` (Task 6).
- Produces — new `Engine` methods:
  - `void sendTransportCommand(uint64_t sourceId, TransportCommand cmd);`
  - `void setSenderVolumeDb(uint64_t sourceId, float dB);`
  - Each marshals via `runOnReactor`, looks up the `Source` by id under `m_mutex`, and — only if it is live and has a non-empty `activeRemote` — calls `m_dacp.send(dacpId, activeRemote, dacpPath(cmd)/dacpVolumePath(dB))`. A missing/lingering source is a no-op.
  - The `Engine` constructor gains a trailing optional parameter `platform::IServiceResolver* resolver = nullptr`; when null and the engine owns its reactor it constructs a `DnsSdResolver` (macOS) / `NullServiceResolver` otherwise — **Ruling point:** keep the factory choice beside the existing advertiser wiring; tests always inject a resolver.

- [ ] **Step 1: Write the failing test**: construct the engine (external reactor) with an injected `NullServiceResolver` seeded to a capturing `TcpListener` (reuse Task 7's harness); start a live session with a known `activeRemote`; call `sendTransportCommand(id, TransportCommand::next)`; pump; assert the listener captured a GET for `/ctrl-int/1/nextitem` carrying that `Active-Remote`.
- [ ] **Step 2: Run to verify it fails** — FAIL.
- [ ] **Step 3: Implement** the two methods + `DacpClient`/resolver ownership.
- [ ] **Step 4: Write the failing test for the ended-source case**: end the session (so it lingers with no live stream / cleared `activeRemote`), call `sendTransportCommand`; assert nothing is sent and no crash. (Covers Review Focus #5.)
- [ ] **Step 5: Run to verify both pass** — `ctest --test-dir build -R engine` → PASS, then whole suite PASS.
- [ ] **Step 6: Commit** — `git commit -m "core: Engine remote-control API (transport + sender volume via DACP)"`.

---

## Task 9: Font embedding + neumorphic theme (app)

**Files:**
- Modify: `app/CMakeLists.txt` (`juce_add_binary_data(TapeInData SOURCES Resources/fonts/Boldonse-Regular.ttf)`, link `TapeInData`)
- Create: `app/Source/Theme.h`, `app/Source/Theme.cpp`
- Create: `core/include/tapein/core/util/format.h`, `core/src/util/format.cpp` (testable helper); add to `core/CMakeLists.txt`
- Test: `tests/unit/format_test.cpp` (register)

**Interfaces:**
- Produces:
  - `std::string tapein::core::util::formatClock(uint32_t seconds);` → `"m:ss"` (e.g. 61 → `"1:01"`, 0 → `"0:00"`), used by the UI for elapsed/duration.
  - `app`-only (JUCE): `tapein::app::Theme` namespace with the palette constants (background `0xff16181c`, slot `0xff1a1d22`, accent `0xffe0a35c`, text `0xffe9ebee`, muted `0xff9097a0`), `juce::Typeface::Ptr boldonse()` (from `BinaryData`), and `struct NeumorphicLookAndFeel : juce::LookAndFeel_V4` plus free draw helpers `drawRaised(g, bounds, radius)` / `drawInset(...)` used by the components.

- [ ] **Step 1: Write the failing test** `format_test.cpp`: `CHECK(formatClock(61)=="1:01"); CHECK(formatClock(0)=="0:00"); CHECK(formatClock(600)=="10:00");`.
- [ ] **Step 2: Run to verify it fails** — `ctest --test-dir build -R format` → FAIL.
- [ ] **Step 3: Implement** `formatClock`; then add the binary-data target and `Theme.{h,cpp}` (compiled only with `TAPEIN_BUILD_APP`).
- [ ] **Step 4: Run to verify the test passes** — PASS; then **build the app** `cmake --build build --target TapeIn` → links `BinaryData::Boldonse_Regular_ttf`.
- [ ] **Step 5: Commit** — `git commit -m "app: embed Boldonse, add neumorphic theme; core: formatClock helper"`.

---

## Task 10: SourceRow → neumorphic slot (app)

**Files:**
- Rewrite: `app/Source/SourceRow.h`, `app/Source/SourceRow.cpp`
- Modify: `app/Source/LevelMeter.cpp` (restyle to the inset meter; no API change)

**Interfaces:**
- Consumes: `SourceSnapshot` (now with metadata/artwork/elapsed/duration), `Theme` (Task 9), `core::util::formatClock`, `TransportCommand` (Task 6).
- Produces — `SourceRow` keeps `onGain/onMute/onSolo` and gains:
  - `std::function<void(uint64_t, core::session::TransportCommand)> onTransport;`
  - `std::function<void(uint64_t, float /*dB*/)> onSenderVolume;`
  - Renders per the design: artwork (decode `snap.artwork` via `juce::ImageFileFormat`, placeholder reel glyph on empty/failure), track/artist/album labels (ellipsized), a state pill, prev/play-pause/next transport buttons (icons drawn in code; play vs pause from `snap.state`), the source-volume slider (emits `onSenderVolume`), the `LevelMeter`, the gain fader, and `S`/`M`. Fixed row height raised to ~132 px.

- [ ] **Step 1: Build check as the test** — this is a JUCE view: write the component, then `cmake --build build --target TapeIn`. Expected: builds clean. *(No unit test; UI verified by build + the Task 11 smoke run. State this in the ledger.)*
- [ ] **Step 2: Implement** the rewritten `SourceRow` + meter restyle.
- [ ] **Step 3: Build** — `cmake --build build --target TapeIn` → success.
- [ ] **Step 4: Commit** — `git commit -m "app: neumorphic source slot with artwork, metadata and transport"`.

---

## Task 11: MainComponent → vertical shell + wiring (app)

**Files:**
- Rewrite: `app/Source/MainComponent.h`, `app/Source/MainComponent.cpp`
- Modify: `app/Source/Main.cpp` (window min size to ~500×600; `MainWindow` resize limits)

**Interfaces:**
- Consumes: `SourceRow` (Task 10) incl. `onTransport`/`onSenderVolume`; `Engine::sendTransportCommand`/`setSenderVolumeDb` (Task 8); `Theme`.
- Produces: the vertical window — top bar (Boldonse `TAPE IN` wordmark + reel mark, centered output-device `ComboBox`, settings gear button), a `Viewport` of slots, and the buffer-mode segmented footer. `timerCallback` keeps the 30 Hz `pollEvents()` + `rebuildRows(sources())` loop; `rebuildRows` additionally wires `row->onTransport = [this](auto id, auto cmd){ m_app.engine()->sendTransportCommand(id,cmd); }` and `row->onSenderVolume = [this](auto id, auto dB){ m_app.engine()->setSenderVolumeDb(id,dB); }`.

- [ ] **Step 1: Implement** the rewritten shell + wiring; keep the Local-Network permission banner and empty state.
- [ ] **Step 2: Build** — `cmake --build build --target TapeIn` → success.
- [ ] **Step 3: Smoke-run** — launch the `.app`, confirm it starts, advertises, and renders the vertical shell with the device selector and footer (loopback sender optional). Record the result in the ledger.
- [ ] **Step 4: Commit** — `git commit -m "app: vertical neumorphic main window wired to remote control"`.

---

## Task 12: Settings restyle + docs + manual test procedure

**Files:**
- Modify: `app/Source/Settings.*` / the settings surface in `MainComponent` (restyle to the neumorphic modal: network name → `setName`; output device + buffer mode; start-at-login). The auxCord "Restrict access" / "Exclusive solo" items are **out of scope** this phase — do not add non-functional controls. *(Ruling: ship only settings backed by real behavior.)*
- Create: `docs/phase-reports/phase-3.md`
- Modify: `CLAUDE.md` (Phase status), `README.md` (feature list), `.github/workflows/ci.yml` only if a new test label needs wiring (it should not — new unit tests auto-discover).

**Interfaces:** consumes everything above.

- [ ] **Step 1: Implement** the settings restyle (functional subset only).
- [ ] **Step 2: Build + full suite** — `cmake --build build` and `ctest --test-dir build` → all green; build the universal app.
- [ ] **Step 3: Write** `docs/phase-reports/phase-3.md` — summary, the maintainer's iPhone acceptance procedure (play from the iPhone; confirm track/artist/album + artwork appear; press play/pause/next/previous in the app and confirm the iPhone responds; move the source-volume slider and confirm the iPhone volume follows), and known limitations.
- [ ] **Step 4: Update** `CLAUDE.md` Phase status to "Phase 3 built, awaiting acceptance".
- [ ] **Step 5: Commit** — `git commit -m "app: neumorphic settings; docs: Phase 3 report and status"`.

---

## Notes for the executor

- Metadata (Tasks 1–3) and the UI (9–11) deliver visible value even before remote control (4–8) is wired; the chosen order does core-first so the UI's transport buttons have real Engine methods to call. If you need an early visual, Tasks 9–10 can be built against stub Engine methods, but land them in order to keep commits coherent.
- The `_dacp._tcp` resolver (Task 5) is the only genuinely new platform surface; everything else extends existing patterns. On Linux CI the macOS resolver test is `#if defined(__APPLE__)`-guarded and the DACP path falls back to `NullServiceResolver` (commands become no-ops), so CI stays green without Bonjour.
