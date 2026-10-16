# Phase 1: Minimal RAOP Receiver (CLI) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A command-line tool on top of `tapein_core` that advertises an AirPlay-1 (RAOP) receiver, accepts sessions from iPhones (and pyatv), and writes each session's decoded 44.1 kHz/16-bit stereo audio to a WAV file; with CI building and testing the core on macOS, Linux and Windows under sanitizers.

**Architecture:** Pure C++20 core (`core/`) with no JUCE: bounds-checked parsers (RTSP, SDP, RTP, DMAP later), Mbed TLS-backed crypto (Apple-Challenge raw RSA signing, RSA-OAEP key unwrap, AES-128-CBC), Apple's ALAC decoder behind a fixed-buffer wrapper, a sequence-indexed JitterBuffer with hold-back release, and a single-threaded `poll()` reactor owning all sockets. `platform/macos` provides the `dns_sd` advertiser. `tools/tapein-cli` wires them and writes WAV. No audio device, no resampler, no mixer in this phase.

**Tech Stack:** C++20, CMake ≥ 3.25, Catch2 v3.16.1, Mbed TLS 3.6.7, Apple ALAC (`macosforge/alac` @ `c38887c`), dns_sd (macOS system), pyatv (integration test), GitHub Actions.

**Spec:** `docs/SPEC.md` (§6 Phase 1 acceptance criteria), `docs/ARCHITECTURE.md` (§1–§4, §10, §14), `CLAUDE.md` (conventions).

## Global Constraints

- C++20; CMake ≥ 3.25; universal binary via `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`; `CMAKE_OSX_DEPLOYMENT_TARGET=13.0`.
- `core/` and `platform/` never include JUCE or Objective-C headers (`.mm` only under `platform/macos/`); `core/` builds on macOS, Linux (gcc + clang) and Windows (MSVC) without JUCE.
- Every source file starts with `// SPDX-License-Identifier: AGPL-3.0-or-later` and `// Copyright (C) 2026 Tape In contributors`. Vendored code keeps its own header and is listed in `THIRD_PARTY.md`.
- Namespaces `tapein::core`, `tapein::platform`; files `snake_case`; types `PascalCase`; functions `camelCase`; constants `kPascalCase`; core members `m_` prefix.
- No exceptions across the core API (use `Result<T>`); no `using namespace` in headers; parsers are pure functions over `std::span<const uint8_t>`.
- Network input is hostile: explicit length checks before every field read; RTSP header block ≤ 16 KiB, ≤ 64 headers, `Content-Length` ≤ 4 MiB; RTP datagrams > 2048 bytes dropped; minimum lengths 12 (audio), 16 (resend reply), 20 (sync), 32 (timing).
- Sanitizers: `-DTAPEIN_SANITIZE=address,undefined` must build and the suite must pass clean.
- Bundle/product naming: "Tape In"; no "AirPlay" in product names; TXT record exactly `txtvers=1 ch=2 cn=0,1 et=0,1 ek=1 sr=44100 ss=16 tp=UDP vn=3 da=true sv=false md=0,1,2 pw=false sm=false am=TapeIn,1 vs=130.14` (plus `src=<url>`).
- Ask the maintainer before `sudo`, system-component installs, or anything needing his Apple account. `pip` into a venv under the build dir is fine.
- Commits small and descriptive, with the attribution trailer; branch `phase-1/cli-receiver`.

## Review Focus

1. **A sender that sends RTSP requests split across TCP segments, or two requests in one segment** — the parser must reassemble and must not drop or merge requests. Test pinned to Task 3 (`feed in 1-byte chunks` and `two requests in one feed`).
2. **A duplicate or very late RTP packet (sequence far behind the read point) after wrap-around at 0xFFFF** — must be ignored, never written out of order and never reset the buffer. Test pinned to Task 8 (`late packet after wrap is ignored`).
3. **A TEARDOWN or TCP close arriving while UDP packets are still in flight** — the session must stop cleanly, the WAV must be finalised with a correct header, and later datagrams for the closed ports must be dropped. Test pinned to Task 10 (`teardown finalises wav; stray datagram after close is harmless`).
4. **Apple-Challenge with no `=` padding and a 16-byte challenge over IPv6** — the response must use the 16-byte local address and strip padding. Test pinned to Task 5 (`challenge ipv6 vector` and `base64 without padding`).
5. **A second sender while one is playing** — must be accepted as a second session with its own UDP ports and its own WAV, not refused with 453 and not mixed into the first file. Test pinned to Task 10 (`two concurrent sessions get distinct ports and sinks`).

---

## File structure

```
CMakeLists.txt                       top level: options, warnings, sanitizers, subdirs
cmake/CompilerWarnings.cmake         warning set per compiler
cmake/Sanitizers.cmake               TAPEIN_SANITIZE handling
cmake/Dependencies.cmake             FetchContent: Catch2, Mbed TLS (+ mbedtls_config.h)
core/CMakeLists.txt                  tapein_core static lib
core/include/tapein/core/util/result.h          Result<T>, Error, ErrorCode
core/include/tapein/core/util/byte_reader.h     bounds-checked big-endian reads
core/include/tapein/core/util/base64.h          encode/decode tolerant of missing '='
core/include/tapein/core/util/wav_writer.h      16-bit PCM WAV writer
core/include/tapein/core/rtsp/rtsp_message.h    RtspRequest/RtspResponse, Headers
core/include/tapein/core/rtsp/rtsp_parser.h     incremental request parser
core/include/tapein/core/rtsp/sdp.h             AnnounceParams + parseAnnounceSdp
core/include/tapein/core/crypto/raop_key.h      embedded RAOP RSA private key (PEM)
core/include/tapein/core/crypto/apple_challenge.h
core/include/tapein/core/crypto/rsa_oaep.h
core/include/tapein/core/crypto/aes_cbc.h
core/include/tapein/core/codec/alac_decoder.h   wrapper over third_party/alac
core/include/tapein/core/rtp/rtp_packets.h      parse*/build* for RTP/control/timing
core/include/tapein/core/buffer/jitter_buffer.h
core/include/tapein/core/net/socket.h           TcpListener, TcpSocket, UdpSocket
core/include/tapein/core/net/reactor.h          poll loop, timers, wake pipe
core/include/tapein/core/session/pcm_sink.h     IPcmSink interface
core/include/tapein/core/session/rtsp_connection.h
core/include/tapein/core/session/session.h
core/include/tapein/core/session/receiver.h     Receiver + ReceiverConfig + TXT record
core/include/tapein/core/platform/service_advertiser.h  IServiceAdvertiser, ServiceRecord
core/src/...                          one .cpp per header above
platform/CMakeLists.txt
platform/macos/dns_sd_advertiser.{h,cpp}
platform/null/null_advertiser.{h,cpp}  no-op (Linux/Windows CI, tests)
third_party/alac/                     vendored codec/ (+ LICENSE, UPSTREAM)
third_party/mbedtls_config.h          minimal Mbed TLS config
tools/tapein-cli/main.cpp
tests/CMakeLists.txt
tests/unit/*_test.cpp                 one per core unit
tests/integration/pyatv_stream_test.py, tests/integration/make_test_wav.py, tests/integration/wavcompare.py
.github/workflows/ci.yml
THIRD_PARTY.md, docs/LEGAL.md, README.md
```

---

### Task 1: Repository skeleton, build options, CI

**Files:**
- Create: `CMakeLists.txt`, `cmake/CompilerWarnings.cmake`, `cmake/Sanitizers.cmake`, `cmake/Dependencies.cmake`, `core/CMakeLists.txt`, `core/include/tapein/core/version.h`, `core/src/version.cpp`, `tests/CMakeLists.txt`, `tests/unit/version_test.cpp`, `.github/workflows/ci.yml`, `README.md`

**Interfaces:**
- Produces: CMake targets `tapein::core` (static), `tapein_tests` (Catch2 executable, registered with CTest); option `TAPEIN_SANITIZE` (string, e.g. `address,undefined`); option `TAPEIN_BUILD_TOOLS` (ON); `tapein::core::versionString() -> std::string_view` returning `"0.1.0-phase1"`.
- Catch2 v3.16.1 via FetchContent (`FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.16.1)`), `Catch2::Catch2WithMain`.

- [ ] **Step 1: Write the failing test** `tests/unit/version_test.cpp`:
  `TEST_CASE("version string is set") { REQUIRE(tapein::core::versionString() == "0.1.0-phase1"); }`
- [ ] **Step 2: Configure fails** — Run: `cmake -S . -B build` → Expected: error, no CMakeLists yet.
- [ ] **Step 3: Write the CMake files.** Top level: `cmake_minimum_required(VERSION 3.25)`, `project(TapeIn VERSION 0.1.0 LANGUAGES C CXX)`, `CMAKE_CXX_STANDARD 20` required, no extensions, `CMAKE_POSITION_INDEPENDENT_CODE ON`, `include(cmake/CompilerWarnings.cmake)` (clang/gcc: `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror=return-type`; MSVC: `/W4 /permissive- /utf-8`), `include(cmake/Sanitizers.cmake)` (adds `-fsanitize=<list> -fno-omit-frame-pointer` for clang/gcc; `/fsanitize=address` for MSVC when list contains `address`), `include(cmake/Dependencies.cmake)`, `add_subdirectory(core)`, `enable_testing()`, `add_subdirectory(tests)`. `core/CMakeLists.txt`: `add_library(tapein_core STATIC src/version.cpp)`, `add_library(tapein::core ALIAS tapein_core)`, `target_include_directories(... PUBLIC include)`, `target_compile_features(... PUBLIC cxx_std_20)`. `tests/CMakeLists.txt`: `add_executable(tapein_tests unit/version_test.cpp)`, link `tapein::core Catch2::Catch2WithMain`, `include(Catch)` + `catch_discover_tests(tapein_tests)`.
- [ ] **Step 4: Build and run** — Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j && ctest --test-dir build --output-on-failure` → Expected: `100% tests passed, 1 test`.
- [ ] **Step 5: Sanitizer build** — Run: `cmake -S . -B build-asan -DTAPEIN_SANITIZE=address,undefined && cmake --build build-asan -j && ctest --test-dir build-asan` → Expected: pass.
- [ ] **Step 6: CI workflow** `.github/workflows/ci.yml`: jobs `macos` (`macos-15`, `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DTAPEIN_SANITIZE=address,undefined`, Ninja), `linux-gcc` and `linux-clang` (`ubuntu-latest`, ASan+UBSan; clang job also runs a TSan configuration), `windows` (`windows-2025`, MSVC, `-DTAPEIN_SANITIZE=address`, Ninja via `ilammy/msvc-dev-cmd` + `cmake -G Ninja`). Each: configure, build, `ctest --output-on-failure`. Trigger on push and pull_request.
- [ ] **Step 7: README.md** — name, one-paragraph description with "works with AirPlay" and the Apple no-affiliation/trademark line, build instructions, "Phase 1 status" note.
- [ ] **Step 8: Commit** `build: add CMake skeleton, Catch2, sanitizer option and CI matrix`.

### Task 2: Core utilities — Result, ByteReader, Base64

**Files:**
- Create: `core/include/tapein/core/util/result.h`, `.../byte_reader.h`, `.../base64.h`, `core/src/util/base64.cpp`
- Test: `tests/unit/result_test.cpp`, `tests/unit/byte_reader_test.cpp`, `tests/unit/base64_test.cpp`

**Interfaces (Produces):**
```cpp
namespace tapein::core {
enum class ErrorCode { invalidArgument, malformedInput, tooLarge, unsupported, cryptoFailure, ioError, notFound, invalidState };
struct Error { ErrorCode code; std::string message; };
template <class T> class Result {   // holds T or Error; void specialisation
 public:
  Result(T value); Result(Error error);           // implicit
  bool ok() const; explicit operator bool() const;
  T& value(); const T& value() const;             // precondition ok()
  const Error& error() const;                     // precondition !ok()
  template <class U> Result<U> map(auto&& f) const;
};
template <> class Result<void> { ... same without value() ... };
inline Error makeError(ErrorCode, std::string msg);

class ByteReader {                                 // over std::span<const uint8_t>
 public:
  explicit ByteReader(std::span<const uint8_t>);
  size_t remaining() const; size_t position() const;
  bool skip(size_t n);                              // false if out of range, position unchanged
  std::optional<uint8_t>  u8();
  std::optional<uint16_t> u16be();
  std::optional<uint32_t> u32be();
  std::optional<uint64_t> u64be();
  std::optional<std::span<const uint8_t>> bytes(size_t n);
};

std::string base64Encode(std::span<const uint8_t>, bool withPadding);
std::optional<std::vector<uint8_t>> base64Decode(std::string_view);   // accepts missing '=', rejects bad chars
}
```

- [ ] **Step 1: Write failing tests**: `Result holds value`, `Result holds error`, `Result<void> ok`, `ByteReader reads big-endian u16/u32/u64`, `ByteReader refuses reads past end and keeps position`, `base64 round trip`, `base64 decode without padding` (input `"AAEC"` → `{0,1,2}`, input `"AAE"` → `{0,1}`), `base64 rejects invalid characters` (`"AA!A"` → nullopt), `base64Encode(…, false)` strips `=`.
- [ ] **Step 2: Run** `ctest -R "result|byte_reader|base64"` → FAIL (compile error).
- [ ] **Step 3: Implement** the headers above; base64 as a 64-entry table with manual handling of 2/3-char tails.
- [ ] **Step 4: Run** → PASS; also under `build-asan`.
- [ ] **Step 5: Commit** `core: add Result, ByteReader and base64 utilities`.

### Task 3: RTSP message model and incremental parser

**Files:**
- Create: `core/include/tapein/core/rtsp/rtsp_message.h`, `core/src/rtsp/rtsp_message.cpp`, `core/include/tapein/core/rtsp/rtsp_parser.h`, `core/src/rtsp/rtsp_parser.cpp`
- Test: `tests/unit/rtsp_parser_test.cpp`, `tests/unit/rtsp_message_test.cpp`

**Interfaces (Produces):**
```cpp
namespace tapein::core::rtsp {
class Headers {                                     // case-insensitive names, insertion order kept
 public:
  void set(std::string name, std::string value);    // replaces existing
  std::optional<std::string_view> get(std::string_view name) const;
  size_t size() const;
  const std::vector<std::pair<std::string,std::string>>& items() const;
};
struct Request { std::string method, uri, version; Headers headers; std::vector<uint8_t> body; };
struct Response { int status = 200; std::string reason = "OK"; Headers headers; std::vector<uint8_t> body;
                  std::vector<uint8_t> serialize() const; };   // "RTSP/1.0 <status> <reason>\r\n" + headers + "Content-Length" always + "\r\n" + body
inline constexpr size_t kMaxHeaderBlockBytes = 16 * 1024;
inline constexpr size_t kMaxHeaders = 64;
inline constexpr size_t kMaxBodyBytes = 4u * 1024 * 1024;
class Parser {
 public:
  // Appends bytes; returns complete requests in order. On malformed input returns Error and the parser
  // must be discarded (connection closed by caller).
  Result<std::vector<Request>> feed(std::span<const uint8_t> bytes);
};
}
```
Accepted request line: `METHOD SP URI SP RTSP/1.0` (also `HTTP/1.1`, used by iOS for some requests). Header line `Name: value` (colon then optional spaces). Body length from `Content-Length` (digits only, ≤ `kMaxBodyBytes`), else 0.

- [ ] **Step 1: Write failing tests**: `parses OPTIONS with CSeq and Apple-Challenge`, `feed in 1-byte chunks yields the same request`, `two requests in one feed`, `request with body uses Content-Length`, `header lookup is case-insensitive`, `rejects request line with two tokens`, `rejects version other than RTSP/1.0 or HTTP/1.1`, `rejects header block over 16 KiB` (ErrorCode::tooLarge), `rejects more than 64 headers`, `rejects Content-Length over 4 MiB`, `rejects non-numeric Content-Length`, `Response serialize includes Content-Length 0 when no body`, `Response serialize body bytes verbatim`.
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement** parser as a small state machine (`readingHeaders` → `readingBody`) over an internal `std::vector<uint8_t>` buffer, scanning for `\r\n\r\n` only within the first 16 KiB.
- [ ] **Step 4: Run** → PASS, ASan clean.
- [ ] **Step 5: Commit** `core: add RTSP message model and incremental parser`.

### Task 4: SDP (ANNOUNCE) parser

**Files:**
- Create: `core/include/tapein/core/rtsp/sdp.h`, `core/src/rtsp/sdp.cpp`
- Test: `tests/unit/sdp_test.cpp`

**Interfaces (Produces):**
```cpp
namespace tapein::core::rtsp {
struct AnnounceParams {
  std::array<int32_t, 12> fmtp{96, 352, 0, 16, 40, 10, 14, 2, 255, 0, 0, 44100};  // defaults, overwritten by a=fmtp
  bool isPcmL16 = false;                 // a=rtpmap:96 L16/44100/2 present
  std::optional<std::array<uint8_t,16>> aesIv;       // a=aesiv (base64)
  std::optional<std::vector<uint8_t>> rsaAesKey;     // a=rsaaeskey (base64, 256 bytes expected)
  std::optional<uint32_t> minLatency, maxLatency;    // a=min-latency / a=max-latency
  int framesPerPacket() const { return fmtp[1]; } int sampleRate() const { return fmtp[11]; }
  int channels() const { return fmtp[7]; } int bitDepth() const { return fmtp[3]; }
};
Result<AnnounceParams> parseAnnounceSdp(std::span<const uint8_t> body);
}
```
Rules: lines split on `\n` with optional trailing `\r`; `a=fmtp:96 <ints...>` reads up to 12 whitespace-separated ints; error (`malformedInput`) if `aesiv` decodes to ≠ 16 bytes, if `rsaaeskey` decodes to ≠ 256 bytes, if exactly one of the two is present, if `framesPerPacket ∉ [1, 4096]`, `sampleRate ≠ 44100`, `channels ≠ 2`, `bitDepth ≠ 16`.

- [ ] **Step 1: Write failing tests**: `parses iOS style announce` (realistic body with `o=iTunes`, `a=rtpmap:96 AppleLossless`, full fmtp, aesiv, rsaaeskey → values), `pyatv L16 announce sets isPcmL16 and no key`, `defaults when fmtp short` (`a=fmtp:96 352 0` keeps defaults for the rest), `rejects aesiv without rsaaeskey`, `rejects aesiv of wrong length`, `rejects channels != 2`, `CR-only line endings tolerated`.
- [ ] **Step 2: Run** → FAIL. **Step 3: Implement.** **Step 4: Run** → PASS. **Step 5: Commit** `core: add SDP parser for ANNOUNCE`.

### Task 5: Crypto — Mbed TLS integration, RAOP key, Apple-Challenge, RSA-OAEP, AES-CBC

**Files:**
- Modify: `cmake/Dependencies.cmake` (Mbed TLS 3.6.7 FetchContent: `GIT_TAG mbedtls-3.6.7`, `ENABLE_TESTING OFF`, `ENABLE_PROGRAMS OFF`, `MBEDTLS_CONFIG_FILE` → `third_party/mbedtls_config.h`), `core/CMakeLists.txt` (link `mbedtls mbedcrypto` PRIVATE)
- Create: `third_party/mbedtls_config.h` (enable: `MBEDTLS_BIGNUM_C`, `MBEDTLS_RSA_C`, `MBEDTLS_PKCS1_V15`, `MBEDTLS_PKCS1_V21`, `MBEDTLS_PK_C`, `MBEDTLS_PK_PARSE_C`, `MBEDTLS_PEM_PARSE_C`, `MBEDTLS_BASE64_C`, `MBEDTLS_ASN1_PARSE_C`, `MBEDTLS_OID_C`, `MBEDTLS_MD_C`, `MBEDTLS_SHA1_C`, `MBEDTLS_SHA256_C`, `MBEDTLS_AES_C`, `MBEDTLS_CIPHER_C`, `MBEDTLS_CIPHER_MODE_CBC`, `MBEDTLS_ENTROPY_C`, `MBEDTLS_CTR_DRBG_C`, `MBEDTLS_PLATFORM_C`, `MBEDTLS_ERROR_C`; include `mbedtls/check_config.h`), `core/include/tapein/core/crypto/raop_key.h`, `core/src/crypto/raop_key.cpp` (the PEM from AirBass `PrivateKey.pem` / shairport-sync `common.c`, as a string literal), `core/include/tapein/core/crypto/apple_challenge.h`, `core/src/crypto/apple_challenge.cpp`, `core/include/tapein/core/crypto/rsa_oaep.h`, `core/src/crypto/rsa_oaep.cpp`, `core/include/tapein/core/crypto/aes_cbc.h`, `core/src/crypto/aes_cbc.cpp`, `docs/LEGAL.md`, `THIRD_PARTY.md`
- Test: `tests/unit/crypto_test.cpp`

**Interfaces (Produces):**
```cpp
namespace tapein::core::crypto {
std::string_view raopPrivateKeyPem();
class RaopRsaKey {                                  // parsed once, shared
 public:
  static Result<std::shared_ptr<RaopRsaKey>> load();        // from raopPrivateKeyPem()
  size_t modulusBytes() const;                              // 256
  Result<std::vector<uint8_t>> signRawPkcs1v15(std::span<const uint8_t> input) const;   // MBEDTLS_MD_NONE
  Result<std::vector<uint8_t>> decryptOaepSha1(std::span<const uint8_t> cipher) const;  // MBEDTLS_RSA_PKCS_V21 + SHA1
};
using HardwareId = std::array<uint8_t, 6>;
// localAddress: 4 bytes (IPv4) or 16 bytes (IPv6) of the socket's local address, network order.
Result<std::string> appleChallengeResponse(const RaopRsaKey&, std::string_view challengeBase64,
                                           std::span<const uint8_t> localAddress, const HardwareId&);
class AesCbcDecryptor {                             // AES-128-CBC, no padding, IV reset per packet
 public:
  AesCbcDecryptor(std::span<const uint8_t,16> key, std::span<const uint8_t,16> iv);
  // Decrypts the leading multiple-of-16 bytes in place; trailing bytes untouched. Returns false on size error.
  bool decryptPacketInPlace(std::span<uint8_t> payload);
};
}
```
Apple-Challenge construction (ARCHITECTURE §3): decode challenge (≤ 16 bytes, else error) ‖ localAddress ‖ hardwareId; if total < 32, zero-pad to 32; sign; base64 **without** padding.

- [ ] **Step 1: Generate test vectors** with the system `openssl` (LibreSSL) against the key written to a temp file: (a) raw PKCS#1 signature of the 32-byte blob for challenge `"AAECAwQFBgcICQoLDA0ODw"` (bytes 0..15), IPv4 `192.168.1.10`, hwid `0A:0B:0C:0D:0E:0F` via `openssl pkeyutl -sign -inkey key.pem -pkeyopt rsa_padding_mode:pkcs1 -in blob.bin`; (b) same with an IPv6 address `fe80::1` (16 bytes); (c) OAEP ciphertext of the 16-byte key `00112233445566778899aabbccddeeff` via `openssl pkeyutl -encrypt -pubin -pkeyopt rsa_padding_mode:oaep`; (d) AES-CBC ciphertext of 40 bytes (32 encrypted + 8 trailing) via `openssl enc -aes-128-cbc -nopad` on the first 32 bytes. Store hex strings in the test file with the generating commands as comments.
- [ ] **Step 2: Write failing tests**: `key loads and has 256-byte modulus`, `challenge ipv4 vector`, `challenge ipv6 vector`, `base64 without padding in challenge is accepted`, `challenge longer than 16 bytes rejected`, `oaep unwrap vector`, `oaep rejects wrong length`, `aes cbc decrypt leaves tail bytes`, `aes cbc rejects payload shorter than 16`.
- [ ] **Step 3: Run** → FAIL. **Step 4: Implement** with `mbedtls_pk_parse_key`, `mbedtls_rsa_pkcs1_sign(ctx, rng, MBEDTLS_MD_NONE, len, hash, sig)` (ctr_drbg seeded once), `mbedtls_rsa_set_padding(MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA1)` + `mbedtls_rsa_rsaes_oaep_decrypt`, `mbedtls_aes_crypt_cbc` with a copied IV per call.
- [ ] **Step 5: Run** → PASS on Debug and ASan.
- [ ] **Step 6: Write `docs/LEGAL.md`** (key provenance per ARCHITECTURE §9.1, decision of 2026-10-16, where the key lives, how to swap for a user-supplied file) and `THIRD_PARTY.md` (Mbed TLS 3.6.7 Apache-2.0, Catch2 BSL-1.0).
- [ ] **Step 7: Commit** `core: add Mbed TLS crypto (Apple-Challenge, RSA-OAEP unwrap, AES-CBC) and LEGAL.md`.

### Task 6: ALAC decoder wrapper

**Files:**
- Create: `third_party/alac/` (copy `codec/*.c|*.cpp|*.h` from `macosforge/alac` @ `c38887c`, its `APPLE_LICENSE.txt`, `UPSTREAM` note with commit; CMake target `alac_codec` with warnings relaxed), `core/include/tapein/core/codec/alac_decoder.h`, `core/src/codec/alac_decoder.cpp`
- Modify: `core/CMakeLists.txt`, `THIRD_PARTY.md`
- Test: `tests/unit/alac_decoder_test.cpp` (links the vendored `ALACEncoder` for round-trip)

**Interfaces (Produces):**
```cpp
namespace tapein::core::codec {
struct AlacConfig { uint32_t framesPerPacket; uint8_t bitDepth; uint8_t pb, mb, kb; uint8_t channels; uint16_t maxRun;
                    uint32_t maxFrameBytes; uint32_t avgBitRate; uint32_t sampleRate;
                    static AlacConfig fromFmtp(const std::array<int32_t,12>&); std::array<uint8_t,24> magicCookie() const; };
class AlacDecoder {
 public:
  static Result<AlacDecoder> create(const AlacConfig&);
  // out must hold framesPerPacket*channels samples; returns frames decoded (≤ framesPerPacket). Never allocates.
  Result<uint32_t> decode(std::span<const uint8_t> packet, std::span<int16_t> out);
};
}
```
fmtp → cookie mapping (shairport-sync `player.c:224-290`): `[1]=frameLength, [2]=compatibleVersion, [3]=bitDepth, [4]=pb, [5]=mb, [6]=kb, [7]=numChannels, [8]=maxRun, [9]=maxFrameBytes, [10]=avgBitRate, [11]=sampleRate`.

- [ ] **Step 1: Write failing tests**: `fromFmtp maps fields`, `round trip 352-frame stereo sine through ALACEncoder then AlacDecoder is bit-exact`, `truncated packet returns error not crash`, `random bytes (1000 seeds) never crash or write past out` (ASan).
- [ ] **Step 2: Run** → FAIL. **Step 3: Vendor + implement** (`ALACDecoder::Init(cookie)`, `Decode(BitBuffer*, out, frames, channels, &outFrames)`; BitBuffer over the packet).
- [ ] **Step 4: Run** → PASS, ASan clean (if the vendored decoder reads past the packet under fuzzing, add a guard in the wrapper: copy the packet into a fixed 2048+32-byte zero-padded buffer before decoding and document it).
- [ ] **Step 5: Commit** `core: vendor Apple ALAC decoder and add bounds-checked wrapper`.

### Task 7: RTP packet parsers and builders

**Files:**
- Create: `core/include/tapein/core/rtp/rtp_packets.h`, `core/src/rtp/rtp_packets.cpp`
- Test: `tests/unit/rtp_packets_test.cpp`

**Interfaces (Produces):**
```cpp
namespace tapein::core::rtp {
inline constexpr size_t kMaxDatagram = 2048;
enum class PayloadType : uint8_t { timingRequest = 0x52, timingReply = 0x53, sync = 0x54, resendRequest = 0x55, resendReply = 0x56, audio = 0x60 };
struct AudioPacket { uint16_t seq; uint32_t rtpTimestamp; uint32_t ssrc; std::span<const uint8_t> payload; bool isResend; };
struct SyncPacket { bool extension; uint32_t rtpTimestampLessLatency; uint64_t ntpTime; uint32_t rtpTimestampNow; };
struct TimingReply { uint64_t originTime, receiveTime, transmitTime; };   // NTP 32.32
std::optional<PayloadType> payloadType(std::span<const uint8_t>);           // packet[1] & 0x7f
std::optional<AudioPacket> parseAudio(std::span<const uint8_t>);           // 0x60 (≥12 bytes) or 0x56 (≥16, skip 4)
std::optional<SyncPacket>  parseSync(std::span<const uint8_t>);            // ≥20 bytes
std::optional<TimingReply> parseTimingReply(std::span<const uint8_t>);     // ≥32 bytes
std::array<uint8_t, 8>  buildResendRequest(uint16_t seqOfRequest, uint16_t firstMissing, uint16_t count); // 80 D5 seq first count
std::array<uint8_t, 32> buildTimingRequest(uint64_t transmitNtp);                                           // 80 D2 00 07 ... transmit at 24..31
uint64_t ntpFromNanoseconds(uint64_t unixNs); uint64_t nanosecondsFromNtp(uint64_t ntp);
}
```

- [ ] **Step 1: Write failing tests**: `parseAudio reads seq/timestamp/payload`, `parseAudio resend reply skips 4-byte prefix`, `parseAudio rejects 11 bytes`, `parseSync fields and extension flag`, `parseSync rejects 19 bytes`, `parseTimingReply rejects 31 bytes`, `buildResendRequest bytes`, `buildTimingRequest layout`, `ntp conversion round trip`.
- [ ] **Step 2–5**: FAIL → implement with `ByteReader` → PASS → Commit `core: add RTP/control/timing packet parsers and builders`.

### Task 8: JitterBuffer with hold-back release, and WavWriter

**Files:**
- Create: `core/include/tapein/core/buffer/jitter_buffer.h`, `core/src/buffer/jitter_buffer.cpp`, `core/include/tapein/core/util/wav_writer.h`, `core/src/util/wav_writer.cpp`
- Test: `tests/unit/jitter_buffer_test.cpp`, `tests/unit/wav_writer_test.cpp`

**Interfaces (Produces):**
```cpp
namespace tapein::core::buffer {
inline constexpr size_t kSlots = 512; inline constexpr size_t kFramesPerSlot = 352;
struct ReleasedBlock { uint16_t seq; uint32_t rtpTimestamp; bool wasMissing; std::span<const int16_t> pcm; };  // pcm = 352*2 samples (silence if missing)
struct MissingRange { uint16_t first; uint16_t count; };
class JitterBuffer {
 public:
  explicit JitterBuffer(size_t framesPerPacket = kFramesPerSlot);
  void reset();                                                  // forget everything (FLUSH / new session)
  // Returns false if the packet is a duplicate, is behind the release point, or is > kSlots ahead of it.
  bool insert(uint16_t seq, uint32_t rtpTimestamp, std::span<const int16_t> pcm, std::chrono::steady_clock::time_point now);
  // Releases, in order, every slot whose seq is <= newestSeq - holdBackPackets (int16 arithmetic). Missing slots are
  // released as silence with wasMissing=true. Calls fn for each; stops at the first slot not yet releasable.
  void release(uint16_t holdBackPackets, std::chrono::steady_clock::time_point now, const std::function<void(const ReleasedBlock&)>& fn);
  // Gaps between the release point and newestSeq that are older than firstRequestAfter and not asked within repeatEvery.
  std::vector<MissingRange> dueResendRequests(std::chrono::steady_clock::time_point now,
                                              std::chrono::milliseconds firstRequestAfter, std::chrono::milliseconds repeatEvery);
  std::optional<uint16_t> newestSeq() const; size_t pendingPackets() const;
  void flushBefore(uint32_t rtpTimestamp);                      // drop slots with timestamp < given
};
}
namespace tapein::core::util {
class WavWriter {    // 16-bit PCM, little-endian, header patched on close
 public:
  static Result<WavWriter> open(const std::filesystem::path&, uint32_t sampleRate, uint16_t channels);
  Result<void> write(std::span<const int16_t> interleaved);
  Result<void> close(); uint64_t framesWritten() const; ~WavWriter();   // close() in dtor if open
};
}
```
Hold-back default used by the session: `holdBackPackets = 63` (≈ 500 ms at 352 frames), `firstRequestAfter = 100 ms`, `repeatEvery = 250 ms`.

- [ ] **Step 1: Write failing tests**: `in-order packets release after hold-back`, `out-of-order packet fills gap before release`, `missing packet released as silence with wasMissing`, `late packet after wrap is ignored` (seq 0xFFF0.. then 0x0005, then re-insert 0xFFF2 after it was released → insert returns false, no re-emit), `duplicate returns false`, `dueResendRequests coalesces runs and respects timing`, `flushBefore drops older timestamps`, `WavWriter writes valid header with correct sizes` (read back bytes 4–7 and 40–43).
- [ ] **Step 2–5**: FAIL → implement (slot array `{seq, ts, filled, firstSeen, lastRequested}`, read pointer, int16 difference arithmetic) → PASS incl. ASan → Commit `core: add JitterBuffer with hold-back release and WavWriter`.

### Task 9: Sockets and reactor

**Files:**
- Create: `core/include/tapein/core/net/socket.h`, `core/src/net/socket.cpp` (one file, `#ifdef _WIN32` for Winsock init/`closesocket`/`WSAPoll`/`ioctlsocket`), `core/include/tapein/core/net/reactor.h`, `core/src/net/reactor.cpp`
- Test: `tests/unit/socket_test.cpp`, `tests/unit/reactor_test.cpp`

**Interfaces (Produces):**
```cpp
namespace tapein::core::net {
struct Endpoint { std::array<uint8_t,16> address{}; bool isV6 = false; uint16_t port = 0;
                  std::string toString() const; std::span<const uint8_t> addressBytes() const; /* 4 or 16 */ };
using NativeHandle = /* int or SOCKET */;
class TcpSocket { public: static Result<TcpSocket> connect(const Endpoint&); Result<size_t> read(std::span<uint8_t>); Result<size_t> write(std::span<const uint8_t>);
                  Result<Endpoint> localEndpoint() const; Result<Endpoint> peerEndpoint() const; void setKeepAlive(std::chrono::seconds idle); NativeHandle handle() const; void close(); };
class TcpListener { public: static Result<TcpListener> bind(const Endpoint& local /* port 0 = dynamic */); Result<TcpSocket> accept(); Result<Endpoint> localEndpoint() const; NativeHandle handle() const; };
class UdpSocket { public: static Result<UdpSocket> bind(const Endpoint& local /* port 0 */); Result<size_t> sendTo(std::span<const uint8_t>, const Endpoint&);
                  Result<std::pair<size_t, Endpoint>> recvFrom(std::span<uint8_t>); Result<Endpoint> localEndpoint() const; NativeHandle handle() const; };
// All sockets are non-blocking; read/recv return 0 bytes with ok() when nothing is available (EWOULDBLOCK).
class Reactor {
 public:
  using FdCallback = std::function<void()>; using TimerId = uint64_t;
  void add(NativeHandle, FdCallback onReadable); void remove(NativeHandle);
  TimerId addTimer(std::chrono::milliseconds after, std::function<void()>, bool repeat);
  void cancelTimer(TimerId);
  void post(std::function<void()>);        // thread-safe; runs on the reactor thread (wake pipe / loopback UDP on Windows)
  void runOnce(std::chrono::milliseconds maxWait); void run(); void stop();   // stop() is thread-safe
};
}
```

- [ ] **Step 1: Write failing tests**: `TcpListener binds dynamic port and accepts a connection`, `UdpSocket round trip on loopback`, `localEndpoint reports v4 bytes`, `Reactor fires readable callback`, `Reactor timer fires once and repeating timer fires at least 3 times`, `post from another thread runs on reactor and stop ends run`.
- [ ] **Step 2–5**: FAIL → implement → PASS (Linux/macOS; Windows path exercised by CI) → Commit `core: add non-blocking sockets and poll reactor`.

### Task 10: RTSP connection, session and receiver

**Files:**
- Create: `core/include/tapein/core/session/pcm_sink.h`, `core/include/tapein/core/session/rtsp_connection.h`, `core/src/session/rtsp_connection.cpp`, `core/include/tapein/core/session/session.h`, `core/src/session/session.cpp`, `core/include/tapein/core/session/receiver.h`, `core/src/session/receiver.cpp`, `core/include/tapein/core/platform/service_advertiser.h`, `platform/CMakeLists.txt`, `platform/null/null_advertiser.{h,cpp}`
- Test: `tests/unit/receiver_test.cpp` (scripted client over loopback using `TcpSocket`/`UdpSocket` from Task 9)

**Interfaces (Produces):**
```cpp
namespace tapein::core::platform {
struct ServiceRecord { std::string instanceName; std::string type; uint16_t port; std::vector<std::pair<std::string,std::string>> txt; };
class IServiceAdvertiser { public: virtual ~IServiceAdvertiser() = default; virtual Result<void> publish(const ServiceRecord&) = 0;
                           virtual void unpublish() = 0; virtual std::optional<net::NativeHandle> pollHandle() const = 0; virtual void processEvents() = 0; };
}
namespace tapein::core::session {
struct SessionInfo { uint64_t id; std::string clientName; std::string dacpId; std::string activeRemote; std::string userAgent; net::Endpoint peer; bool encrypted; bool pcm; };
class IPcmSink { public: virtual ~IPcmSink() = default;
  virtual void onSessionStarted(const SessionInfo&) = 0;
  virtual void onPcm(uint64_t sessionId, const buffer::ReleasedBlock&) = 0;      // network thread, in order
  virtual void onVolume(uint64_t sessionId, float dB) = 0;
  virtual void onSessionEnded(uint64_t sessionId, std::string_view reason) = 0; };
struct ReceiverConfig { std::string name = "Tape In"; crypto::HardwareId hardwareId; uint16_t rtspPort = 0; size_t maxSessions = 100;
                        uint16_t holdBackPackets = 63; std::string sourceUrl = "https://github.com/..."; std::string txtEt = "0,1"; bool answerAppleChallenge = true; };
class Receiver {
 public:
  Receiver(net::Reactor&, ReceiverConfig, IPcmSink&, platform::IServiceAdvertiser&, std::shared_ptr<crypto::RaopRsaKey>);
  Result<void> start();            // bind listener, publish service, add to reactor
  void stop();                     // teardown all sessions, unpublish
  uint16_t rtspPort() const; platform::ServiceRecord serviceRecord() const;   // "<HWID>@<name>", _raop._tcp, TXT per Global Constraints + src=
  size_t sessionCount() const;
};
}
```
Behaviour (ARCHITECTURE §3): per connection `rtsp::Parser`; every request: `CSeq` echoed, `Server: AirTunes/105.1`, Apple-Response if header present and `answerAppleChallenge`; OPTIONS → `Public:` list; ANNOUNCE → `parseAnnounceSdp`, unwrap key, create Session (or 453 if `maxSessions` reached); SETUP → parse `Transport` for `control_port`/`timing_port`, bind 3 `UdpSocket`s on the connection's local address, reply `Transport: RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;control_port=C;timing_port=T;server_port=S` + `Session: 1`; RECORD → `Audio-Latency: 11025`, start timing requests (3 × 300 ms then 3 s), start release timer (10 ms); FLUSH → `flushBefore(rtptime)`; SET_PARAMETER `text/parameters` `volume:` → `onVolume`; other SET_PARAMETER/GET_PARAMETER → 200; TEARDOWN → end session, `Connection: close`; unknown method → 501; SETUP/RECORD before ANNOUNCE → 455. Audio datagrams: `payloadType` → `parseAudio` → decrypt (if encrypted) → decode (ALAC) or byte-swap L16 → `JitterBuffer::insert`; control socket: `parseSync` (store latency `now − lessLatency`), resend replies (0xD6 → treat as audio); timing socket: `parseTimingReply` (log offset only). Session ends on TEARDOWN, TCP EOF/error, or 30 s without audio after RECORD. On end: release everything remaining (hold-back 0), then `onSessionEnded`.

- [ ] **Step 1: Write failing tests** (scripted client, `NullAdvertiser`, a `RecordingSink` that stores events): `OPTIONS gets Public and Apple-Response matching crypto vector`, `ANNOUNCE SETUP RECORD yields Transport with three distinct dynamic ports and sink gets onSessionStarted`, `unencrypted L16 packets arrive at sink in order after hold-back`, `ALAC packets decrypted and decoded` (encrypt a known packet with AES-CBC in the test using Mbed TLS), `missing packet triggers resend request on client control port`, `FLUSH drops older packets`, `volume SET_PARAMETER reaches sink`, `teardown finalises; stray datagram after close is harmless`, `two concurrent sessions get distinct ports and sinks`, `SETUP before ANNOUNCE returns 455`, `garbage on TCP closes connection without crash`, `maxSessions=1 second ANNOUNCE gets 453`.
- [ ] **Step 2–5**: FAIL → implement (`RtspConnection` owns parser + socket; `Session` owns UDP sockets, decryptor, decoder, JitterBuffer, timers; `Receiver` owns listener, connections, sessions, advertiser) → PASS incl. ASan and TSan (single-threaded, but `post`/`stop` are cross-thread) → Commit `core: add RTSP connection, session and receiver`.

### Task 11: macOS dns_sd advertiser

**Files:**
- Create: `platform/macos/dns_sd_advertiser.h`, `platform/macos/dns_sd_advertiser.cpp` (plain C++ against `<dns_sd.h>`, no Obj-C needed), `platform/CMakeLists.txt` (target `tapein_platform`, links `tapein::core`; on Apple adds the macOS file; always adds null)
- Test: `tests/unit/dns_sd_advertiser_test.cpp` (macOS only, opt-in via env `TAPEIN_RUN_MDNS_TESTS=1`)

**Interfaces (Produces):** `tapein::platform::DnsSdAdvertiser : core::platform::IServiceAdvertiser` using `DNSServiceRegister` with `kDNSServiceFlagsNoAutoRename` **not** set (let Bonjour rename on collision) and TXT built with `TXTRecordCreate/SetValue`; `pollHandle()` = `DNSServiceRefSockFD`; `processEvents()` = `DNSServiceProcessResult`; maps `kDNSServiceErr_PolicyDenied` to `ErrorCode::unsupported` with message "Local Network permission denied".

- [ ] **Step 1: Write failing test**: `publish then browse finds the instance with expected TXT` (uses `DNSServiceBrowse` + `DNSServiceResolve` in-process with a 5 s timeout; skipped unless env set).
- [ ] **Step 2–5**: FAIL → implement → run with `TAPEIN_RUN_MDNS_TESTS=1` locally → PASS → Commit `platform: add macOS dns_sd service advertiser`.

### Task 12: tapein-cli

**Files:**
- Create: `tools/CMakeLists.txt`, `tools/tapein-cli/main.cpp`, `tools/tapein-cli/wav_sink.{h,cpp}` (IPcmSink → one `WavWriter` per session, file `<outDir>/<timestamp>-<clientName>-<id>.wav`), `tools/tapein-cli/args.{h,cpp}`

**Interfaces (Produces):** executable `tapein-cli` with flags `--name <str>` (default "Tape In"), `--out-dir <path>` (default `.`), `--port <n>` (default 0), `--hwid <12hex>` (default: derived from `--name` via FNV-1a, locally-administered bit set), `--txt-et <str>` (default `0,1`; experiment), `--no-apple-response` (experiment), `--verbose` (log every RTSP request/response and the Apple-Challenge inputs/outputs as hex), `--max-sessions <n>`. Exit 0 on SIGINT/Ctrl-C after clean stop; prints the chosen port and the service name at start; prints `session <id> started/ended` and per-session packet/missing/resend counters at end.

- [ ] **Step 1: Write failing test** `tests/unit/cli_args_test.cpp`: `parses defaults`, `rejects bad hwid`.
- [ ] **Step 2–4**: implement args → PASS; implement `main.cpp` (Reactor + Receiver + DnsSdAdvertiser or NullAdvertiser on non-Apple + WavSink; SIGINT handler calls `reactor.stop()` via `post`).
- [ ] **Step 5: Smoke run** — `./build/tools/tapein-cli/tapein-cli --name "Tape In Dev" --verbose` then in another shell `dns-sd -B _raop._tcp` → Expected: instance `<HWID>@Tape In Dev` listed; `dns-sd -L` shows the TXT record and port.
- [ ] **Step 6: Commit** `tools: add tapein-cli (receiver to WAV)`.

### Task 13: Integration test with pyatv (unencrypted PCM path)

**Files:**
- Create: `tests/integration/make_test_wav.py` (20 s: 1 kHz tone bursts with silence gaps + a 50 ms click at t=1 s for alignment; 44.1 kHz/16-bit/stereo; deterministic), `tests/integration/wavcompare.py` (align by cross-correlation on the click, then compare sample-by-sample; report max abs diff, number of differing samples, and leading offset; exit 0 iff ≤ 0 differing samples after alignment, else 1 with a histogram), `tests/integration/pyatv_stream_test.py` (creates venv-free: expects `atvremote` on PATH; starts `tapein-cli --port 0 --out-dir tmp`, reads the printed port, runs `atvremote --manual --address 127.0.0.1 --port <p> --protocol raop --id <hwid> stream_file=<wav>`, waits for exit + 3 s, runs wavcompare), `tests/integration/requirements.txt` (`pyatv==<latest 0.16.x>`), `tests/integration/README.md`
- Modify: `.github/workflows/ci.yml` (macOS job: `python3 -m venv build/venv && build/venv/bin/pip install -r tests/integration/requirements.txt`, then run the script), `tests/CMakeLists.txt` (`add_test(NAME pyatv_integration ...)` guarded by `TAPEIN_INTEGRATION=ON`)

- [ ] **Step 1: Generate and inspect the test WAV locally**; run wavcompare on the file against itself → `0 differing samples`.
- [ ] **Step 2: Create a venv under `build/venv` and `pip install pyatv`** (no system install). Run the integration script locally → Expected: PASS, or a documented diff (e.g. pyatv pads the stream start with silence → alignment handles it; any dropped-packet count must be 0 on loopback).
- [ ] **Step 3: Wire into CI** (macOS job only; Linux job runs it too with `NullAdvertiser` since pyatv is pointed at the port manually).
- [ ] **Step 4: Commit** `test: add pyatv integration test comparing sent and received WAV`.

### Task 14: Encrypted path check with raop_play (best effort) and session capture

**Files:**
- Create: `tests/integration/raop_play_test.sh` (builds `philippe44/libraop` into `build/libraop` if `TAPEIN_LIBRAOP=1`, streams a WAV with `raop_play -e`, runs wavcompare), note in `tests/integration/README.md`
- Modify: `tools/tapein-cli` `--verbose` already logs the challenge inputs; add `--capture <dir>` writing `rtsp-<id>.log` (full request/response text) and `rtp-<id>.bin` (length-prefixed raw datagrams) for later replay/fuzz seeds.

- [ ] **Step 1**: implement `--capture`; unit test `capture writes length-prefixed datagrams` in `tests/unit/cli_capture_test.cpp`.
- [ ] **Step 2**: run `raop_play -e` locally if it builds on this Mac in < 15 min; otherwise mark the script as manual and record why in the README. CI: not required (license unstated, build cost).
- [ ] **Step 3: Commit** `tools: add session capture; test: add raop_play encrypted-path script`.

### Task 15: Fuzz harnesses

**Files:**
- Create: `tests/fuzz/CMakeLists.txt` (built only when `TAPEIN_FUZZ=ON` and compiler is clang: `-fsanitize=fuzzer,address,undefined`), `tests/fuzz/rtsp_parser_fuzz.cpp`, `tests/fuzz/sdp_fuzz.cpp`, `tests/fuzz/rtp_fuzz.cpp` (routes by first byte to the three parsers), `tests/fuzz/alac_fuzz.cpp`, `tests/fuzz/corpus/` seeded from the unit-test inputs
- Modify: `.github/workflows/ci.yml` (Linux clang job: build fuzzers and run each for 60 s)

- [ ] **Step 1**: write the four `LLVMFuzzerTestOneInput` harnesses (≤ 15 lines each).
- [ ] **Step 2**: run each locally for 2 minutes → Expected: no crash; if a crash appears, fix the parser, add the input as a unit test, re-run.
- [ ] **Step 3: Commit** `test: add libFuzzer harnesses for RTSP, SDP, RTP and ALAC`.

### Task 16: Phase 1 report and manual test instructions

**Files:**
- Create: `docs/phase-reports/phase-1.md` (what was built, how to test with iPhone incl. the `et=0` experiment procedure, known problems, counters to look at), update `README.md` and `CLAUDE.md` phase status, `THIRD_PARTY.md` complete.

- [ ] **Step 1**: write the report; verify every command in it by running it.
- [ ] **Step 2: Full verification** — `ctest` on Debug, ASan/UBSan, and `git status` clean; CI green on the pushed branch if a remote exists (if not, note it).
- [ ] **Step 3: Commit** `docs: Phase 1 report`.

---

## Self-review notes

- Spec coverage: SPEC §6 Phase 1 items map to Tasks 10–13 (receiver, Control Center visibility via Task 11/12 smoke, `dns-sd -B` via Task 12, WAV comparison via Task 13, Windows CI via Task 1). "10 minutes without dropouts" is a manual criterion in Task 16's instructions, with counters from Task 12. ARCHITECTURE §14's `et=0` experiment is covered by Task 12 flags + Task 16 procedure; the capture tool by Task 14; fuzzing by Task 15.
- Not in this phase (by design): resampler, drift control, mixer, DACP, metadata, audio device, JUCE, Windows advertiser, login item.
- Type consistency checked: `Result`, `ByteReader`, `HardwareId`, `ReleasedBlock`, `Endpoint`, `IPcmSink`, `IServiceAdvertiser` names are used identically across tasks.
