# Phase 3 — Exact UI + Access Control Implementation Plan

> **For agentic workers:** executed inline (native) per the maintainer's standing preference. Backend tasks are TDD (RED→GREEN). UI tasks are painting/layout code, verified by build + launch (no meaningful unit test), matching the approved artboards pixel-for-pixel.

**Goal:** Re-skin the Tape In app to match the four approved neumorphic artboards exactly, and add the backends the approved Settings modal implies: real RTSP password protection ("Restrict access"), exclusive solo, and settings persistence. "Output 1/Output 2" is shown but inert (multichannel is out of v1 scope).

**Architecture:** Core gains a pure, fuzzable Digest-auth module (mbedtls MD5) and an auth gate in the RTSP receiver; the Engine gains `setRequirePassword` and an exclusive-solo mode, flowing from `Settings` through `EngineConfig` exactly like `ignoreSenderVolume`. The JUCE app rewrites `Theme`/`SourceRow`/`MainComponent` and adds a `DeviceButton` dropdown and a `SettingsModal` overlay.

**Tech Stack:** C++20, JUCE 9.0.4, Mbed TLS 3.6.7, Catch2 v3, libFuzzer (Linux CI) + CTest corpus replay.

**Spec:** docs/SPEC.md §1/§2/§6. Design: approved artboards in claude.ai Design canvas (Main / Empty / DeviceMenu / Config).

## Global Constraints

- Converse pt-BR; code/comments/identifiers/commits/docs in English.
- SPDX header on every file: `// SPDX-License-Identifier: AGPL-3.0-or-later` + `// Copyright (C) 2026 Tape In contributors`.
- `core/` and `platform/` never include JUCE.
- Every network byte is hostile: explicit length checks, bounded sizes, parsers pure over `std::span`/`std::string_view`, fuzzed. The Authorization header is already length-bounded by the RTSP parser (`kMaxHeaderBlockBytes`).
- No locks/alloc/IO/logging on the audio thread; solo changes reach the audio thread via `std::atomic<bool>` only.
- Never use "AirPlay" in product strings; descriptive "Works with AirPlay" with the no-affiliation notice only.
- Password stored as plaintext in settings.xml (matches existing hardwareId practice); note it in the report.
- Exact palette (from artboards): bg `#16181c`, slot `#1a1d22`, inset `#15181c`, accent `#e0a35c`, accentPress `#d4914a`, text `#e9ebee`, muted `#9097a0`, faint `#6d7680`, dim `#454d57`, dimmer `#4e565f`, playing `#5fcf86`, hairline `rgba(255,255,255,0.045)`. Boldonse for the wordmark / section titles.

## Review Focus

- **Malformed Authorization header** (missing quotes, duplicate keys, absurd lengths, no `Digest ` prefix, `%`-escapes) → parser returns nullopt, never crashes/allocates unboundedly. (Task 1 tests + fuzz.)
- **Wrong password / replayed or forged nonce** → request stays 401; a nonce we never issued is rejected even if the response digest is internally consistent. (Task 2 tests.)
- **Password toggled on while a sender is mid-session** → `rebuildReceiver()` re-advertises `pw=true`; existing streams are not torn (match `setIgnoreSenderVolume`). (Task 3.)
- **Exclusive solo race** → enabling solo on A clears B atomically under `m_mutex`; the audio thread sees a consistent gate (no window where both or neither are soloed). (Task 3 tests.)
- **Empty password with Restrict access on** → treated as "not configured": do not 401 (would lock out everyone); advertise `pw=false`. (Task 2/3.)

---

## Task 1: Digest-auth core module (pure, TDD + fuzz)

**Files:**
- Modify: `third_party/mbedtls_config.h` (add `#define MBEDTLS_MD5_C`)
- Create: `core/include/tapein/core/crypto/digest_auth.h`, `core/src/crypto/digest_auth.cpp`
- Modify: `core/CMakeLists.txt` (register `src/crypto/digest_auth.cpp`)
- Test: `tests/unit/digest_auth_test.cpp` (register in `tests/CMakeLists.txt`)
- Fuzz: `tests/fuzz/digest_auth_fuzz.cpp` + `tests/fuzz/corpus/digest_auth/` (register name in `tests/fuzz/CMakeLists.txt:11`)

**Interfaces — Produces:**
```cpp
namespace tapein::core::crypto {
struct DigestCredentials { std::string username, realm, nonce, uri, response; };
// Parse `Authorization: Digest key="v", key=v, ...` (value after the header name,
// or the whole header). Returns nullopt on anything malformed. Pure.
std::optional<DigestCredentials> parseDigestAuthorization(std::string_view header);
// response = MD5( MD5(username:realm:password) : nonce : MD5(method:uri) ), lowercase hex.
std::string computeDigestResponse(std::string_view username, std::string_view realm,
                                  std::string_view password, std::string_view nonce,
                                  std::string_view method, std::string_view uri);
// realm & nonce must equal expected; response must match computeDigestResponse. Constant-time compare.
bool verifyDigest(const DigestCredentials& cred, std::string_view password,
                  std::string_view method, std::string_view expectedRealm, std::string_view expectedNonce);
std::string generateNonce(); // 16 random bytes, lowercase hex; std::random_device-seeded.
}
```

- [ ] Step 1: `mbedtls_config.h` — add `#define MBEDTLS_MD5_C` near the other `MBEDTLS_*_C` hash lines (31-34).
- [ ] Step 2: Write `digest_auth_test.cpp` (failing): known RFC-2617 vector for `computeDigestResponse`; `parseDigestAuthorization` reads all five fields from a realistic iOS header and tolerates spaces/mixed order; returns nullopt for `""`, no-`Digest`-prefix, unterminated quote, a key with no `=`; `verifyDigest` true on a self-consistent credential and false on wrong password, wrong realm, wrong nonce.
- [ ] Step 3: Run it — FAIL (link/symbol missing).
- [ ] Step 4: Implement `digest_auth.cpp`. MD5 via `mbedtls/md.h` (`mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_MD5), …)`). Parser: scan `Digest ` prefix (case-insensitive), then split on commas at top level, each `key` or `key="value"`/`key=value`, trim (reuse rtsp-style trim), bounded by input length (no recursion, no growth). Reject on structural errors. `verifyDigest` uses a byte-wise constant-time compare.
- [ ] Step 5: Run unit test — PASS. Run full `tapein_tests` — green.
- [ ] Step 6: Add `digest_auth_fuzz.cpp` (feeds bytes to `parseDigestAuthorization`, and the parsed-or-synthetic fields to `computeDigestResponse`), seed `corpus/digest_auth/` (one real header, one empty, one truncated). Register name at `tests/fuzz/CMakeLists.txt:11`. Run `fuzz_regress_digest_auth` under CTest — PASS.
- [ ] Step 7: Commit.

## Task 2: RTSP auth gate + `pw` TXT flag (TDD)

**Files:**
- Modify: `core/include/tapein/core/session/receiver.h` (ReceiverConfig: `bool requirePassword=false; std::string password;`)
- Modify: `core/include/tapein/core/rtsp/rtsp_connection.h` (add `std::string authNonce;`)
- Modify: `core/src/session/receiver.cpp` (auth gate in `onRequest`; 401 helper; `reasonFor` 401; `pw` in `serviceRecord()`)
- Test: `tests/unit/receiver_test.cpp`

**Interfaces — Consumes:** Task 1's `crypto::parseDigestAuthorization`, `verifyDigest`, `generateNonce`.

- [ ] Step 1: Tests (failing): (a) extend the TXT test (receiver_test.cpp:122) — with `requirePassword=true` + non-empty password, `txt["pw"] == "true"`; with empty password it stays `"false"`. (b) New: `requirePassword` on, `ANNOUNCE` with no `Authorization` → `401`, `WWW-Authenticate` present with `Digest realm=` and `nonce=`; `OPTIONS` is still answered (exempt). (c) resend `ANNOUNCE` with an `Authorization` computed (via `crypto::computeDigestResponse`) from the issued nonce + correct password → not 401. (d) wrong password → stays 401.
- [ ] Step 2: Run — FAIL.
- [ ] Step 3: Implement. `reasonFor`: add `case 401: return "Unauthorized";`. `serviceRecord()` line ~188: `{"pw", (m_config.requirePassword && !m_config.password.empty()) ? "true" : "false"}`. In `onRequest`, before the method ladder and after the activity note: if `requirePassword` && password non-empty && method != "OPTIONS": read `Authorization`; if absent/invalid (`!verifyDigest(cred, password, method, realm, conn.authNonce)`), set `conn.authNonce = crypto::generateNonce()`, send `makeResponse(conn, request, 401)` + `WWW-Authenticate: Digest realm="Tape In", nonce="<conn.authNonce>"`, and return. realm = fixed `"Tape In"`.
- [ ] Step 4: Run receiver tests — PASS. Full suite — green.
- [ ] Step 5: Commit.

## Task 3: Engine `setRequirePassword` + exclusive solo (TDD)

**Files:**
- Modify: `core/include/tapein/core/session/engine.h` (`EngineConfig`: `bool exclusiveSolo=false;`; declare `void setRequirePassword(bool, std::string); void setExclusiveSolo(bool);`)
- Modify: `core/src/session/engine.cpp` (`setRequirePassword` → set `m_config.receiver.requirePassword/password` + `rebuildReceiver()`; `setExclusiveSolo`; exclusive logic in `setSolo`)
- Test: `tests/unit/engine_test.cpp`

**Interfaces — Consumes:** Task 2's `ReceiverConfig` fields.

- [ ] Step 1: Tests (failing): with exclusive solo on, soloing source B clears source A's solo (both the registry flag and, where a stream exists, the atomic); with it off, both can be soloed. `setRequirePassword(true, "x")` makes the next advertised record carry `pw=true` (assert via the receiver/advertiser the engine fixture exposes, or via `sources()`/a getter if that is how the fixture observes config — else assert no throw + state).
- [ ] Step 2: Run — FAIL.
- [ ] Step 3: Implement `setRequirePassword` (mirror `setName`: lock, mutate `m_config.receiver`, `rebuildReceiver()`), `setExclusiveSolo` (store in `m_config`), and in `setSolo` when enabling and `m_config.exclusiveSolo`, loop `m_sources` clearing every other source's `solo` + atomic (mirror the `setIgnoreSenderVolume` loop).
- [ ] Step 4: Run engine tests — PASS. Full suite — green.
- [ ] Step 5: Commit.

## Task 4: Settings persistence + AppEngine wiring (TDD where testable, else direct)

**Files:**
- Modify: `app/Source/Settings.h`, `app/Source/Settings.cpp` (`requirePassword`/`setRequirePassword`, `password`/`setPassword`, `exclusiveSolo`/`setExclusiveSolo`)
- Modify: `app/Source/AppEngine.cpp` (initial `cfg.receiver.requirePassword/password`, `cfg.exclusiveSolo`)

- [ ] Step 1: Add the three getter/setter pairs following `ignoreSenderVolume` (bools, default false) and `receiverName` (password string, default "").
- [ ] Step 2: In `AppEngine::start()` config assembly, set `cfg.receiver.requirePassword = m_settings.requirePassword(); cfg.receiver.password = m_settings.password().toStdString(); cfg.exclusiveSolo = m_settings.exclusiveSolo();`.
- [ ] Step 3: Build app target — compiles. Commit.

## Task 5: Theme — icons, controls, modal helpers (direct)

**Files:** `app/Source/Theme.h`, `app/Source/Theme.cpp`

Add to `tapein::app::theme`:
- `icons` namespace: pure path-drawing helpers taking `(Graphics&, Rectangle<float>, Colour, strokeWidth)` for `reel`, `speakerLow`, `speakerHigh`, `gear`, `chevronDown`/`chevronUp`, `check`, `stack` (empty-slot), and transport `prev`/`play`/`pause`/`next`. Geometry traced from the artboard SVGs (24×24 viewBox, scaled into bounds).
- `drawSegmentedWell` / segmented-button paint helpers for the buffer footer (inset trough + raised active pill).
- `dimmer` colour `0xff4e565f`.
- `NeumorphicLookAndFeel`: keep `drawLinearSlider` (amber fill + knob matches the artboard volume track).

- [ ] Implement; build. (No unit test — painting.)
- [ ] Commit.

## Task 6: SourceRow — exact slot + empty/placeholder mode (direct)

**Files:** `app/Source/SourceRow.h`, `app/Source/SourceRow.cpp`

- Add `void setEmpty(int slotIndex)` (placeholder: opacity ~0.6, `stack` icon in a `#15181c` well, title "Slot", dashed "// waiting for a device", greyed transport, empty vtrack, greyed S/M) and keep `update(snapshot)` for live sources.
- Live layout exactly: art 56 well (artwork or disc placeholder) | mid { top row: title 15/700 + subtitle 12 `#9097a0` on left, `● <state>` colored text with glow dot, NO pill background, right; ctrls row: prev(32)/play(38 amber)/next(32) ghost circles, then vol group `speakerLow` + slider(amber) + `speakerHigh` } | S/M column (30×26, radius 8, on = amber gradient + dark text).
- [ ] Implement; build + launch; eyeball against Main/Empty artboards. Commit.

## Task 7: DeviceButton + output dropdown (direct)

**Files:** Create `app/Source/DeviceButton.h`/`.cpp`; used by `MainComponent`.

- Centered raised pill: `speakerLow` icon + current output name + `chevronDown`. Click → styled `PopupMenu` (slot bg, hover `#20242a`, amber `check` on the current device). Selecting calls back `onSelect(name)`. Matches DeviceMenu artboard.
- [ ] Implement; build + launch. Commit.

## Task 8: SettingsModal overlay (direct)

**Files:** Create `app/Source/SettingsModal.h`/`.cpp`.

- Full-bounds component: scrim `rgba(8,9,11,0.6)` + card (top 92, side 28, radius 20, big shadow). Title "SETTINGS" in Boldonse amber. Grid (120px right-aligned labels): Network name (inset text input); Restrict access (28×28 inset checkbox, amber check); **Password** (inset text input, `setPasswordCharacter`, shown when Restrict access is on); Exclusive solo (checkbox); Audio device (raised dropdown → same output list); Output 1 / Output 2 (two raised dropdowns, **inert**, fixed labels, tooltip "coming soon"). Save button (amber gradient) applies + closes.
- Callbacks out: `onApply(name, requirePassword, password, exclusiveSolo, outputName)`; `onClose()`. Wire to `Settings` + `Engine` (`setName`, `setRequirePassword`, `setExclusiveSolo`, `audio().selectOutput`).
- [ ] Implement; build + launch. Commit.

## Task 9: MainComponent — top bar, slots-to-3, footer trough, modal (direct)

**Files:** `app/Source/MainComponent.h`, `app/Source/MainComponent.cpp`

- Top bar: brand (reel + Boldonse "TAPE IN" + "RECEIVER" small-caps `#6d7680`), centered `DeviceButton`, gear icon button (custom `gear` paint; toggles the modal).
- Slots: render `max(3, numSources)` rows; first `numSources` are live (`update`), the rest `setEmpty(i)`. Remove the old centered `m_empty` label.
- Footer: "BUFFER" label + segmented well (3 buttons in an inset trough) + right status `"<N> sources · ~<latency> s latency"`, or `"waiting for a device…"` when none.
- Replace the fold-down settings panel with the `SettingsModal` overlay (child, full bounds, visible when gear is on).
- [ ] Implement; build + launch; eyeball against all four artboards. Commit.

## Task 10: Window size (direct)

**Files:** `app/Source/Main.cpp`

- Default content size 500×726 (artboard is 500×760 incl. a 34px titlebar; native title bar supplies the real one). Keep resizable with sensible limits.
- [ ] Implement; build + launch. Commit.

## Final

Run full `tapein_tests` green; fuzz regression green; build + launch the app and confirm it advertises and does not crash. Fresh whole-branch review (most capable model). Fix Critical/Important with TDD. Report rulings + deferred minors + the iPhone acceptance test for password (toggle Restrict access + set password → iPhone prompts for it).
