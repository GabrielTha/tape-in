# Tape In — Phase-0 research: platform, toolchain and library choices

Date: 2026-10-16. Scope: macOS first (JUCE app + pure-C++ core), Windows port and AU/VST3 plugins later. Licence of the project: AGPLv3.
Everything marked **[inference]** is my reading of the evidence, not something a source states outright. Everything else is sourced inline (URL + date observed, 2026-10-16 unless noted).

## Recommendations summary

| # | Topic | Recommendation | Confidence |
|---|-------|----------------|------------|
| 1 | JUCE version / licence | Use **JUCE 9.0.x** (8.x went security-only on 2026-07-21) under the **AGPLv3** option; no splash/attribution requirement; CMake 3.22+, Xcode 12.4+ (practically Xcode 16/26), `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`; keep the core a plain `add_library(... STATIC)` that never links a `juce::` target | High (licence, CMake); Medium (JUCE 9 vs 8 — API churn risk) |
| 2 | Minimum macOS | **13.0 Ventura** as deployment target (it is the API floor anyway: SMAppService), with 14+ as the "tested/supported" tier; revisit when a needed API is 14-only | Medium |
| 3 | Login item | `SMAppService.mainApp` from Objective-C++; app must be code-signed (Developer ID in practice); no documented /Applications requirement; notarisation required only for daemons (but you notarise anyway). Windows: `HKCU\...\Run` with an `asInvoker` manifest | High |
| 4 | mDNS | macOS: `dns_sd.h` directly (DNSServiceRegister/Browse/Resolve, own select loop via `DNSServiceRefSockFD`). Windows: **native windns.h DNS-SD** (Win10 1809+) behind the same interface; **mjansson/mdns** (public domain) as fallback; do **not** bundle Apple's Bonjour for Windows | Medium-High |
| 5 | Crypto | **Mbed TLS 3.6 LTS** (Apache-2.0 OR GPL-2.0+) vendored via FetchContent, used on both OSes and in the core's unit tests; platform APIs (Security.framework / CNG) can do all three ops but are not worth the two code paths | High |
| 6 | ALAC | David Hammerton's MIT decoder as carried (and bug-fixed) in shairport-sync `alac.c`; Apple's Apache-2.0 `macosforge/alac` as fallback (archived, read-only) | High |
| 7 | Resampler | **libsamplerate 0.2.2** (BSD-2) sinc converters with per-block `src_ratio` (documented smooth interpolation); fallback **zita-resampler VResampler** (GPL-3, AGPL-compatible) | Medium-High |
| 8 | Plugin SDKs | VST3 3.8 is **MIT**; ASIO SDK now **GPLv3 OR proprietary** (Oct 2025) so JUCE's ASIO backend can ship in an AGPL build; promise **WASAPI (shared+exclusive) + ASIO** on Windows | High |
| 9 | App→plugin IPC (v2) | Keep the audio path transport-agnostic (SPSC ring over a byte region). Plan POSIX shm (unsandboxed hosts) + loopback-UDP fallback (sandboxed hosts); study discoLink (MIT, 2026) | Medium |
| 10 | CI | `macos-15`/`macos-26` (arm64; universal build via CMake), `windows-2025` (VS 2022 17.14, clang-cl 20), plus `ubuntu-latest` for the core's ASan/UBSan/TSan; public repo = free; 5 concurrent macOS jobs | High |
| 11 | Signing | Developer ID Application + hardened runtime (no extra entitlements needed for an output-only network audio app) + `notarytool submit --wait` + staple `.app` then `.dmg` | High |

---

## 1. JUCE licensing, minimums, CMake API

### Licence
- JUCE modules are "dual-licensed under the AGPLv3 and the commercial JUCE licence"; anyone *not* using the AGPLv3 option is bound by the JUCE **9** EULA (the LICENSE.md on `master` already says "JUCE 9 End User Licence Agreement"). Examples are ISC. Third-party deps are listed in `JUCE.spdx.json`. (https://raw.githubusercontent.com/juce-framework/JUCE/master/LICENSE.md)
- Commercial tiers (https://juce.com/get-juce/): Starter free (< $20k revenue), Indie $40/mo or $800 perpetual (< $300k), Pro $175/mo or $3,500 perpetual, Educational free. The page restates: "If you are not propagating or conveying closed-source software containing JUCE outside your organisation, you may still be within AGPLv3's terms."
- **Mixing tiers:** The two licences are alternatives for a given work; you choose one per product. An AGPLv3 Tape In therefore uses JUCE under AGPLv3 only; a later closed-source fork would need a commercial licence **[inference from LICENSE.md wording "If you are not licensing the JUCE Framework modules under the AGPLv3 … you agree to the terms of the JUCE 9 EULA"]**.
- **Splash screen / attribution:** JUCE 8 removed the splash screen entirely; the JUCE 6/7 "personal licence requires splash" rule is gone (JUCE forum, https://forum.juce.com/t/licensing-for-a-free-plug-in/66179). Neither juce.com nor LICENSE.md mentions any attribution requirement for JUCE 8/9. Under AGPLv3 your only obligations are the AGPL's own: ship/offer Corresponding Source, keep notices, and §13's "network interaction" clause.
- **AGPL §13 and an AirPlay receiver [inference]:** §13 obliges you to offer source to "users interacting with it remotely through a computer network" if the program "supports such interaction". An iPhone streaming to Tape In arguably is such interaction. Cheap, defensible compliance: put the source URL in the RTSP `Server:` header and in the `_raop._tcp` TXT record (e.g. a `src=` key) and in the About box. This costs nothing and removes any argument.
- **Binaries (DMG) and plugins:** AGPL allows binary distribution; the DMG must carry the licence text and a source offer (link to the tagged GitHub release is the usual practice). AU/VST3 plugins built on JUCE-under-AGPL are fine: VST3 SDK is now MIT (see §8) and Apple's AU headers are part of the SDK. Nothing in the JUCE EULA touches you if you are on the AGPL path.
- Caveat from LICENSE.md: "The JUCE team cannot provide compliance confirmations, nor any other legal advice."

### Which JUCE?
- Releases (https://github.com/juce-framework/JUCE/releases, observed 2026-10-16): **9.0.0 on 21 Jul 2026** ("new macOS CoreAudio implementation", new SVG parser, variable fonts), 9.0.1 (10 Aug), 9.0.2 (7 Sep, adds SECURITY.md + SBOM), 9.0.3 (28 Sep), **9.0.4 (12 Oct 2026, latest)**. JUCE 8's last feature release was **8.0.15 (21 Jul 2026)**; **8.0.16-security (13 Oct 2026)** is a security-only patch, and the master BREAKING_CHANGES.md lists no toolchain/deployment-target breaks for 9.0.x (https://raw.githubusercontent.com/juce-framework/JUCE/master/BREAKING_CHANGES.md).
- **Recommendation:** start on **9.0.4** rather than 8. The brief says "JUCE 8", but 8 is now in security maintenance, 9 has the rewritten CoreAudio device layer (relevant to an audio-output app), and no breaking change in 9.0.x touches the modules Tape In needs (`juce_gui_basics`, `juce_gui_extra`, `juce_audio_devices`, `juce_audio_basics`). Risk: a new CoreAudio implementation may have early bugs; mitigate by pinning the tag and keeping the core JUCE-free so a downgrade to 8.0.16 is a one-line CMake change.

### Minimums (same text in the 8.0.16 README and the master/9.x README)
- Build: C++17, **Xcode 12.4** ("Intel macOS 10.15.4, Apple Silicon macOS 11.0"), **Visual Studio 2019**, **CMake 3.22+**. Deployment: **macOS 10.11 (x86_64, arm64)**, Windows 10 1607 (8.0.13 bumped the Windows floor to 1607). (https://raw.githubusercontent.com/juce-framework/JUCE/8.0.16-security/README.md and https://raw.githubusercontent.com/juce-framework/JUCE/master/README.md)
- Nothing in `juce_audio_devices` / `juce_gui_basics` needs newer than that floor **[inference: the READMEs state a single framework-wide floor and BREAKING_CHANGES lists no per-module bump]**. Modern Xcode (16.x/26) only *offers* ≥ 11.0 in the UI, but `CMAKE_OSX_DEPLOYMENT_TARGET` accepts 13.0 without issue (JUCE forum, https://forum.juce.com/t/best-way-to-build-with-support-for-older-macos-versions/64879).

### CMake API (https://raw.githubusercontent.com/juce-framework/JUCE/master/docs/CMake%20API.md)
- `juce_add_gui_app(TapeIn PRODUCT_NAME "Tape In" BUNDLE_ID app.tapein.mac COMPANY_NAME ... VERSION ...)`. Link modules **PRIVATE** (`juce::juce_gui_extra juce::juce_audio_devices juce::juce_audio_utils`) and the flag interface targets `juce::juce_recommended_config_flags juce::juce_recommended_warning_flags juce::juce_recommended_lto_flags`. Linking modules PUBLIC "can cause module sources to be built multiple times".
- `juce_add_plugin(TapeInPlugin FORMATS AU VST3 Standalone PLUGIN_MANUFACTURER_CODE Tpin PLUGIN_CODE Tapi ...)` creates a static "shared code" target plus one target per format; `COPY_PLUGIN_AFTER_BUILD`, `VST3_AUTO_MANIFEST` (moduleinfo.json), `AU_COPY_DIR`/`VST3_COPY_DIR`, and `juce_enable_copy_plugin_step()` if you need to sign before copying. AU is macOS-only; VST3 needs nothing extra.
- Hardened runtime: `HARDENED_RUNTIME_ENABLED TRUE` and `HARDENED_RUNTIME_OPTIONS "<com.apple.security.* keys>"`; `APP_SANDBOX_*` options exist but you will not sandbox a Developer-ID app.
- `juce_add_module(<path>)` adds a module as an INTERFACE library; `juce_generate_juce_header()` is optional.
- **Pure-C++ core:** nothing JUCE-specific is needed. `add_library(tapein_core STATIC src/...)`, `target_compile_features(tapein_core PUBLIC cxx_std_20)`, `target_link_libraries(TapeIn PRIVATE tapein_core)`. Because the JUCE flag targets are INTERFACE targets, the core will not inherit JUCE's `-D` definitions unless you link them — keep it that way so the core can be built and unit-tested on a Linux runner with no JUCE checkout. Put JUCE itself behind `FetchContent`/`add_subdirectory` *only* in the `app/` subtree.
- **Universal binary:** JUCE's doc explicitly says configure with `-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64`; JUCE's CMake handles the rest (fat `.app`, plugin bundles). Use the Xcode generator for signing convenience or Ninja with manual `codesign`.

---

## 2. Minimum macOS version: 13 vs 14

Constraints collected:
- **SMAppService** (login item) is `API_AVAILABLE(macos(13.0))` (local SDK header `ServiceManagement/SMAppService.h`). That is the hard API floor for the "launch at login" feature; below 13 you would need the deprecated `SMLoginItemSetEnabled` + helper bundle.
- **Local Network privacy** was introduced in **macOS 15** (TN3179, https://developer.apple.com/documentation/technotes/tn3179-understanding-local-network-privacy, revised 2026-10-06). It is a *runtime* behaviour, not a deployment-target constraint: on 15+ "Registering a service with Bonjour" requires local-network access, so Tape In will trigger the prompt the first time it calls `DNSServiceRegister` for `_raop._tcp`. You need `NSLocalNetworkUsageDescription` and `NSBonjourServices` (`_raop._tcp`, `_airplay._tcp`, `_dacp._tcp`) in Info.plist regardless of the minimum you pick. Listening/accepting TCP and receiving unicast UDP do *not* require the privilege; multicast receive does (relevant if you ever listen for mDNS yourself).
- **Ventura support ended in 2025:** macOS 13.7.7 (late July 2025) was its last security update; Safari 26 was not released for it (heise, https://heise.de/-10513577); endoflife tracks 2025-09-15 as EOL (https://endoflife.ai/article-macos-eol).
- **Install base:** StatCounter is useless here (it reports post-Catalina Macs as "Catalina"/"Cheetah"; https://gs.statcounter.com/macos-version-market-share). TelemetryDeck's app-telemetry sample, quoted for late July 2026, shows macOS 15.7 at 9.23% and Sonoma 14.8 at 0.80%, with Tahoe dominating and Ventura not in the top ten (https://telemetrydeck.com/survey/apple/macOS/versions/, via the search summary). **[inference]** Ventura is at most ~1–2% of active Macs in late 2026 and falling; Sonoma a few percent.
- **Toolchains:** Xcode 16/26 SDKs can target 13.0 (deployment-target string, see §1); JUCE 9's floor is 10.11; GitHub's macOS runners are 15/26 but that only affects *building*.
- **Product angle [inference]:** an AirPlay receiver's natural use case includes "turn the old Mac into a speaker". The Macs stuck on Ventura are 2017-era Intel machines — precisely that audience. That argues for *not* raising the floor above the API floor.

**Recommendation: deployment target 13.0; officially "supported & tested" 14+; 13 as best-effort.** Rationale: 13 is the API floor you already need (SMAppService); nothing else in the v1 design (dns_sd, CoreAudio via JUCE, Security.framework ≥10.12, CommonCrypto) needs 14; the only cost is a Ventura VM in your personal test matrix (CI cannot run it). Choose 14 only if you find a must-have 14-only API; none surfaced in this research. Confidence: medium (driven by the install-base inference).

---

## 3. Login item

### macOS — `SMAppService.mainApp` (13+)
From the SDK header (`SMAppService.h`, macOS SDK on this machine) and Apple DTS (https://developer.apple.com/forums/thread/777520):
- "Apps that use SMAppService APIs must be code signed." "Apps that contain LaunchDaemons must be notarized" — the notarisation sentence is specific to daemons; for a login item only signing is required (and `kSMErrorInvalidSignature` is returned "If the app bundle is not properly code signed"). **[inference]** Ad-hoc signing may work for local testing (one forum poster says signing "was needed during testing") but ship Developer ID, which you need for Gatekeeper and Local Network privacy anyway.
- No /Applications requirement is documented. Community reports say that moving/renaming the app after registering yields `.notFound` ("the system can't find the registration"); re-register on launch if status is `.notFound` **[inference from forum reports, https://developer.apple.com/forums/thread/768592]**.
- Objective-C++ usage (DTS's own snippet):
  ```objc
  @import ServiceManagement;
  SMAppService *svc = [SMAppService mainAppService];
  NSError *err = nil;
  BOOL ok = [svc registerAndReturnError:&err];     // kSMErrorAlreadyRegistered if already on
  ok = [svc unregisterAndReturnError:&err];
  SMAppServiceStatus st = svc.status;              // NotRegistered / Enabled / RequiresApproval / NotFound
  [SMAppService openSystemSettingsLoginItems];     // deep-link when st == RequiresApproval
  ```
  `SMAppServiceErrorDomain` as an exported constant is 15.0+, but the error codes are the classic `kSMError*` values and work on 13.
- User-facing behaviour: the app appears in **System Settings > General > Login Items & Extensions** under the app's name; if the user toggles it off, `status` becomes `.requiresApproval` (header: "If the user has denied execution, the return value will be SMAppServiceRequiresApproval"). macOS posts a notification "Tape In added items that can run in the background" on first registration **[inference, standard behaviour since 13]**. Known wart: unregister immediately followed by register can fail; add a short delay (forum thread 768592).

### Windows
- `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` string value `"Tape In" = "C:\...\TapeIn.exe" --autostart`. No elevation needed to write HKCU.
- UAC caveat: Windows refuses to auto-start from Run/Startup any executable whose manifest says `requireAdministrator` (Microsoft UAC docs, https://learn.microsoft.com/zh-tw/previous-versions/bb325654(v=msdn.10); Old New Thing). Tape In's manifest must be `asInvoker` — it should be anyway.
- Users can disable the entry in Settings > Apps > Startup / Task Manager; Windows records that under `HKCU\...\Explorer\StartupApproved\Run`, so read that key too before reporting "enabled" in your UI **[inference, well-known behaviour; not re-verified this session]**.

---

## 4. mDNS / DNS-SD

### macOS — `dns_sd.h`
- Present in the SDK (`_DNS_SD_H 2881120011` on this machine). API you need: `DNSServiceRegister(&ref, 0 /*or kDNSServiceFlagsNoAutoRename*/, kDNSServiceInterfaceIndexAny, "AABBCCDDEEFF@Tape In", "_raop._tcp", NULL, NULL, htons(port), TXTRecordGetLength(&txt), TXTRecordGetBytesPtr(&txt), cb, ctx)` with `TXTRecordCreate/TXTRecordSetValue` for `txtvers=1 ch=2 cn=0,1 et=0,1 sv=false da=true sr=44100 ss=16 pw=false vn=3 tp=UDP md=0,1,2 vs=130.14 am=TapeIn,1 sf=0x4`; `DNSServiceBrowse(..., "_dacp._tcp", ...)` then `DNSServiceResolve` (pass the browse callback's `interfaceIndex`) to find the sender's DACP remote-control port.
- Run-loop integration: (a) `DNSServiceSetDispatchQueue(ref, queue)` — easiest for an ObjC++ app; must `DNSServiceRefDeallocate` on the same queue; (b) `DNSServiceRefSockFD(ref)` + `select/poll/kqueue` + `DNSServiceProcessResult(ref)` — the right choice for a pure-C++ core that already owns a network thread; header warns that calling `DNSServiceProcessResult` with no data blocks. Use `kDNSServiceFlagsShareConnection` to multiplex register+browse on one socket.
- Local Network privacy (15+): the first `DNSServiceRegister` triggers the prompt; while undetermined/denied the operation waits with `kDNSServiceErr_PolicyDenied` (-65570) — detect it and show guidance (TN3179).

### Windows options
1. **Native windns.h DNS-SD** (`DnsServiceRegister/DeRegister`, `DnsServiceBrowse`, `DnsServiceResolve`, `DnsServiceConstructInstance` with `keys/values` arrays for TXT; `dnsapi.lib`). Microsoft's page lists "Minimum supported client: Windows 10 [desktop apps only]" and that registration is asynchronous and "tied to the lifetime of the calling process" (https://learn.microsoft.com/en-us/windows/win32/api/windns/nf-windns-dnsserviceregister). The functions appeared with the 1809 SDK (10.0.17763) and are guarded accordingly in `windns.h` **[inference — widely reported; Microsoft's docs do not print the build number and I could not fetch the header]**; Windows 10 1607–1803 and all Server 2016 are out of support anyway, so "Windows 10 1809+" is a reasonable published requirement. Known caveats: Browse returns instance names only — always Resolve; callbacks arrive on thread-pool threads; the DNS-SD APIs are disabled entirely when a Hyper-V virtual switch is present (ctrl.blog, 2015 — may be fixed, https://ctrl.blog/entry/windows-mdns-dnssd); the responder is the system "DNS Client" service, so **no inbound firewall rule is needed from your app** (Windows ships an "mDNS (UDP-In)" rule for svchost). The instance string is the full `name._raop._tcp.local` — RAOP's `MAC@Name` form has no DNS-SD-illegal characters, but escape any `.` in the user-chosen name **[inference]**. I found no confirmed bug reports about TXT records or custom names with this API.
2. **Apple's Bonjour SDK for Windows / mDNSResponder:** the *source* is Apache-2.0 (https://developer.apple.com/licensing-trademarks/bonjour/ → github.com/apple-oss-distributions/mDNSResponder) and Apache-2.0 is GPLv3/AGPLv3-compatible, so in principle you could build mDNSResponder yourself. But the *binary* "Bonjour for Windows" installer/SDK is under Apple's proprietary "Bonjour for Windows Bundling Agreement" (separate program, forum thread https://developer.apple.com/forums/thread/702815 asks about it and got no answer). Redistributing Apple's installer in an AGPL DMG/MSI is not something you can do without that agreement, and building mDNSResponder's Windows service yourself is a large, poorly-documented job. Avoid.
3. **mjansson/mdns** — header-only C, "Public Domain" (Unlicense per Gentoo/Fedora packagers), registration + discovery primitives, but *you* run the socket loop and answer queries (https://github.com/mjansson/mdns). Needs its own UDP 5353 multicast socket (SO_REUSEADDR alongside Windows' DNS Client) and thus an **inbound firewall rule**, which requires elevation to add — the installer would have to create it. Also coexists imperfectly with the OS responder (two responders answering for the same host) **[inference]**.

**Recommendation:** an `IServiceAdvertiser`/`IServiceBrowser` interface in the core; `dns_sd` backend on macOS; **native windns backend on Windows** (zero dependencies, no firewall work, OS-owned responder), with **mjansson/mdns as the fallback backend** if the native one proves unreliable in testing (its public-domain licence makes vendoring trivial). Windows minimum: 10 1809. Firewall: your RTSP TCP port and RTP UDP ports still need inbound rules on Windows (Windows prompts automatically for the TCP listener on first run; UDP you should add via the installer).

---

## 5. Crypto

What RAOP needs: (a) RSA-2048 PKCS#1 v1.5 *raw* signature of the Apple-Challenge blob (no hash, no DigestInfo); (b) RSA-OAEP(SHA-1, MGF1-SHA-1) decryption of the AES key from `rsaaeskey`; (c) AES-128-CBC decrypt, no padding (trailing partial block is sent in clear).

### macOS Security.framework / CommonCrypto (all 10.12+, so fine for a 13 floor)
- Key import: `SecKeyCreateWithData` accepts RSA in "PKCS#1 format" (header comment). The well-known AirPort key is a PKCS#1 `RSA PRIVATE KEY` PEM, so base64-decode and pass it — no PKCS#8 unwrapping needed. Attributes: `kSecAttrKeyType=kSecAttrKeyTypeRSA`, `kSecAttrKeyClass=kSecAttrKeyClassPrivate` (size is derived).
- (a) Two routes, both documented in `SecKey.h`: `kSecKeyAlgorithmRSASignatureRaw` — "size of input data must be the same as value returned by SecKeyGetBlockSize()" → you build the PKCS#1 type-1 block (`00 01 FF…FF 00 || challenge`) yourself (trivial, 256 bytes) and raw-sign; or `kSecKeyAlgorithmRSASignatureDigestPKCS1v15Raw` — "assumes that input data is digest and OID … as specified in PKCS#1 v1.5", i.e. it applies v1.5 padding around whatever bytes you pass without adding a DigestInfo; Chromium uses exactly this constant for TLS 1.0/1.1 MD5-SHA1 signatures (https://chromium.googlesource.com/chromium/src/+/main/net/ssl/ssl_platform_key_mac.cc). Both work for software RSA keys; the forum reports of "algorithm not supported" concern smart-card (CTK) keys only (https://developer.apple.com/forums/thread/740684). Apple DTS confirms raw signing "For RSA, yes" (https://developer.apple.com/forums/thread/741030).
- (b) `SecKeyCreateDecryptedData(key, kSecKeyAlgorithmRSAEncryptionOAEPSHA1, cipher, &err)`; header: "data is padded using OAEP padding scheme internally using SHA1", block size = modulus size.
- (c) `CCCrypt(kCCDecrypt, kCCAlgorithmAES, 0 /*options: no kCCOptionPKCS7Padding → no padding*/, key, 16, iv, in, len, out, outCap, &moved)` — padding is opt-in via `kCCOptionPKCS7Padding` (CommonCryptor.h), so passing 0 gives raw CBC; feed only the multiple-of-16 prefix.

### Windows CNG
- Import: CNG wants `BCRYPT_RSAPRIVATE_BLOB` (n, e, p, q) or `BCRYPT_RSAFULLPRIVATE_BLOB`; you must parse the PKCS#1 DER yourself (small ASN.1 walker) **[inference, standard CNG practice]**.
- (a) `BCryptSignHash(key, &BCRYPT_PKCS1_PADDING_INFO{ .pszAlgId = NULL }, challenge, len, sig, 256, &cb, BCRYPT_PAD_PKCS1)` — Microsoft documents: "If this member is NULL, then the OID is not added" (https://learn.microsoft.com/en-us/windows/desktop/api/Bcrypt/ns-bcrypt-bcrypt_pkcs1_padding_info), i.e. raw PKCS#1 v1.5 over arbitrary input. OpenVPN relies on the same trick via `NCryptSignHash` and only saw problems with some hardware tokens truncating to 36 bytes (https://patchwork.openvpn.net/patch/490) — irrelevant for a software key.
- (b) `BCryptDecrypt(key, in, 256, &BCRYPT_OAEP_PADDING_INFO{ BCRYPT_SHA1_ALGORITHM, NULL, 0 }, NULL, 0, out, cap, &cb, BCRYPT_PAD_OAEP)`; CNG uses the same hash for MGF1 (https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/ns-bcrypt-bcrypt_oaep_padding_info).
- (c) `BCryptSetProperty(BCRYPT_CHAINING_MODE, BCRYPT_CHAIN_MODE_CBC)` and `BCryptDecrypt` *without* `BCRYPT_BLOCK_PADDING` → raw CBC.

### Portable libraries
- **Mbed TLS**: 4.2.0 (2026-07-07, latest), **4.1 LTS (to Mar 2029)**, **3.6.7 / 3.6 LTS (to Mar 2027)** (https://github.com/Mbed-TLS/mbedtls/releases; TrustedFirmware tech-forum decks). Licence "Apache-2.0 OR GPL-2.0-or-later" (README) — both AGPLv3-compatible. 4.x moved crypto into the **TF-PSA-Crypto git submodule** and the dev branch needs Python/Perl to generate files; release tarballs are self-contained. 3.6 is a single repo and `FetchContent`s cleanly (`ENABLE_TESTING=OFF`, `ENABLE_PROGRAMS=OFF`, a custom `MBEDTLS_CONFIG_FILE` with only RSA/PK/PEM/AES/SHA-1/MD5/bignum → a few hundred KB of object code **[inference]**). All three ops map 1:1: `mbedtls_pk_parse_key` (PEM directly), `mbedtls_rsa_pkcs1_sign(ctx, rng, MBEDTLS_MD_NONE, len, challenge, sig)` (MD_NONE = raw v1.5 padding, no DigestInfo), `mbedtls_rsa_set_padding(ctx, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA1)` + `mbedtls_rsa_rsaes_oaep_decrypt`, `mbedtls_aes_crypt_cbc`. (4.x retains the low-level `mbedtls_rsa_*` API but removed several legacy layers in favour of PSA — verify before choosing 4.x **[inference]**.)
- **OpenSSL 3** (Apache-2.0): works (`RSA_private_encrypt` is deprecated in 3.x in favour of EVP with `RSA_NO_PADDING` + manual padding), but 3–5 MB of library and a slow build for three primitives. Not worth it.

**Recommendation:** vendor **Mbed TLS 3.6 LTS** via FetchContent and use it on both platforms *inside the core*. Rationale: the core's unit tests (challenge/response vectors captured from a real iPhone, decrypt-known-packet tests) then run identically on macOS/Linux/Windows CI; one code path; no ASN.1 parsing of your own; Apache-2.0 compatible. Keep a `Crypto` interface anyway (5 functions) so a `SecurityFramework`/`CNG` backend can be added if binary-size or policy ever demands it. Confidence: high.

---

## 6. ALAC decoder

- **Apple `macosforge/alac`** (Apache-2.0): archived/read-only, 5 commits, 19 open issues, 2 open PRs, ships Xcode + Visual Studio projects + a UNIX Makefile; README warns VS < 2010 lacks `<stdint.h>` and that non-default packet sizes may not work everywhere (https://github.com/macosforge/alac). Forks: TimothyGu/alac (autotools + VS files, README says "unmaintained"), mikebrady/alac (autotools, used by shairport-sync's optional "Apple ALAC" path). The code is C++ with endianness handled for CAF/WAV I/O; the decoder core is portable **[inference from README; no MSVC-specific defects located]**.
- **shairport-sync `alac.c`**: David Hammerton's 2005 decoder, **MIT** (licence header verified, https://raw.githubusercontent.com/mikebrady/shairport-sync/master/alac.c). ~1,000 lines of C, decodes exactly the 352-frame, 16-bit stereo RAOP stream, exercised for a decade in shairport-sync, which is the de-facto reference AirPlay-1 receiver; its copy carries the accumulated bug fixes (overflow/`bits` handling). Compiles with clang and MSVC as plain C (uses no GNU extensions) **[inference]**.
- ffmpeg's ALAC: LGPL-2.1+, licence-compatible but pulls in libavcodec — rejected on size. OwnTone decodes through ffmpeg too **[inference]**.
- AudioToolbox (`AudioConverter` with `kAudioFormatAppleLossless`) is macOS-only and would leave the Windows core without a decoder and the CI without a Windows test; reject.

**Recommendation:** vendor Hammerton's MIT decoder (take shairport-sync's version, keep the licence header, add a C++ wrapper with a fixed-size output buffer so `decode()` never allocates). Fallback: Apple's Apache-2.0 decoder from the archived repo if a bitstream feature is ever missing. Confidence: high.

---

## 7. Resampler with continuously variable ratio

Requirements: 44.1 kHz → 44.1/48/96 kHz, ratio nudged by ≤ ±0.1 % from buffer-level feedback, no clicks, no allocation in `process()`, licence compatible.

| Library | Licence | Variable ratio | Real-time | Notes |
|---|---|---|---|---|
| **libsamplerate 0.2.2** (2021) | BSD-2 | Yes — `src_process` "tries to smoothly move from the previous call's ratio to the current one" across the block; `src_set_ratio` forces a step (https://libsndfile.github.io/libsamplerate/api_full.html) | Allocations happen in `src_new`; `src_process`/`src_callback_read` do not allocate **[inference from the library's design; confirm by reading `src_sinc.c`]** | Sinc converters (`SRC_SINC_MEDIUM_QUALITY` ≈ 97 dB, `BEST` ≈ 145 dB). Pull model via `src_callback_new/src_callback_read` fits an audio callback |
| libsoxr 0.1.3 | **LGPL-2.1+** | Yes, `SOXR_VR` + `soxr_set_io_ratio(ratio, slew_len)` ramps over `slew_len` samples; max ratio fixed at `soxr_create`; "experimental"; HQ only; slower than fixed mode (README; examples/5-variable-rate.c; VLC patch 2015) | Unknown; VR engine undocumented | Best fixed-ratio quality in the field, but VR is explicitly experimental |
| speexdsp resampler | BSD-3 | `speex_resampler_set_rate_frac(num, den, in, out)` — integer rational only; changing it rebuilds the sinc table (`update_filter`) which can `realloc` → **not** RT-safe on ratio change **[inference from source structure]** | partly | Used by PulseAudio/ALSA rate plugin for adaptive resampling, but they tolerate the realloc |
| r8brain-free-src | MIT | Fixed ratio by design (`CDSPResampler` takes src/dst rates at construction; README offers no runtime ratio API) **[inference]** | yes | Excellent quality, unsuitable for drift tracking |
| zita-resampler (`VResampler`) | **GPL-3** (AGPL-compatible via §13) | Yes — built for exactly this (`set_rratio`), used by zita-ajbridge/zita-a2j for clock-drift compensation | yes (tables allocated in `setup`) | Quality fixed by `hlen`; code small and clean |
| Hand-written windowed-sinc with fractional read pointer | — | Yes | yes | ~300 lines; polyphase table + linear interpolation between phases is adequate at ±0.1 % and 64–128 taps; only worth it if you want zero deps |

**Recommendation:** **libsamplerate** `SRC_SINC_MEDIUM_QUALITY` (or `BEST` on desktop, the CPU cost is trivial for stereo), driven in pull mode from the audio callback, with `data.src_ratio` updated once per callback from the jitter-buffer level (a slow PI controller; clamp to ±0.2 %). The library interpolates the ratio across the block, which is what makes ±0.1 % steps inaudible. Permissive licence, CMake-native, everywhere in the ecosystem. **Fallback:** zita-resampler `VResampler` (GPL-3 is fine for an AGPLv3 project) if you ever need a bounded-latency guarantee that libsamplerate's internal buffering makes awkward. Confidence: medium-high (the no-allocation claim should be verified by reading `src_process` before committing).

---

## 8. Plugin SDK licences for v2

- **VST3 SDK 3.8 is MIT** since Steinberg's press release of **2025-10-29** ("transitions to the MIT Open Source License … from the previous dual licensing model", https://ocl-steinberg-live.steinberg.net/_storage/asset/819253/storage/master/Press%20Release%20-%202025-10-29%20-%20VST%203.8%20-%20EN.pdf; coverage https://librearts.org/2025/11/steinberg-relicenses-vst3-and-asio/). JUCE bundles the VST3 SDK; any older GPLv3-or-proprietary copy was also compatible with AGPLv3 through §13 of each licence (GNU FAQ, https://www.gnu.org/licenses/gpl-faq.html#AllCompatibility — the FAQ page rate-limited my fetch; stating from the well-known matrix).
- **AU**: Apple's AU SDK/headers are part of the macOS SDK; JUCE's wrapper is under JUCE's licence. Nothing to do.
- **ASIO SDK**: the same press release says the ASIO SDK "is available under GPLv3 licensing, allowing both proprietary and open-source licensing options to be offered side by side" — i.e. **GPLv3 OR proprietary** (heise, https://heise.de/-10963451). Before October 2025 the SDK was proprietary-only and could not be redistributed with GPL code (hence the Audacity/JUCE "bring your own SDK" workarounds, https://forum.juce.com/t/gpl-and-asio/9035). Now: an AGPLv3 Tape In may vendor the ASIO SDK headers (under GPLv3) and ship a JUCE build with `JUCE_ASIO=1`. Remaining caveats **[inference]**: (1) the ASIO *trademark* is Steinberg's — using "ASIO" to describe compatibility is normal, but don't put it in the product name; (2) check the exact file-level licence headers of the SDK version you vendor, since press releases are not licences; (3) GPLv3 code inside an AGPLv3 work keeps its GPLv3 terms for that part (§13 permits the combination, it does not relicense).
- Old workarounds if you ever need them: user-supplied SDK path at build time, FlexASIO/ASIO4ALL for users, or WASAPI exclusive mode only.

**Recommendation for Windows:** promise **WASAPI shared + exclusive** as the baseline (JUCE native, no SDK) and **ASIO** as a supported device type in official builds now that the SDK is GPLv3. Document the SDK's dual licence in `THIRD_PARTY.md`. Confidence: high on licences, medium on trademark nuance.

---

## 9. App → plugin IPC (keep v1 from blocking v2)

- **macOS:** POSIX `shm_open`+`mmap` is Apple's recommended IPC shared-memory primitive (System V is discouraged; https://developer.apple.com/forums/thread/719897). Inside the App Sandbox the name must be `GROUP/NAME` where `GROUP` is an app-group ID (≤31 bytes total; denials show as `ipc-posix-shm-write-create`, https://developer.apple.com/forums/thread/751241). An AUv2 plugin runs inside the *host's* process with the *host's* sandbox, so in GarageBand (sandboxed) or any AUv3 context your plugin cannot open a shm segment named after *your* group **[inference from the sandbox rule]**. Logic Pro and most third-party DAWs are not sandboxed, where plain `shm_open("/app.tapein.v1")` works. Mach ports/XPC have the same sandbox-name problem.
- **Windows:** `CreateFileMapping(INVALID_HANDLE_VALUE, …, "Local\\TapeIn.v1")` + `MapViewOfFile` — no sandbox issues for native plugins.
- **Prior art:** **discoLink** (discoDSP, 2026, MIT, C++17, lock-free SPSC rings in shared memory, standalone app registers as a device the plugin connects to — https://www.discodsp.com/discolink/, v0.1). ReaStream and Blue Cat Connector move audio over UDP (also within one machine) which sidesteps sandboxes; Connector documents "resampling and drift compensation" between unsynchronised clocks and latency reporting (v1.4) — exactly the gotchas you will hit (https://bluecataudio.com/Blog/?p=6656). Rogue Amoeba's products use a virtual audio driver (ACE), a different architecture.
- **Gotchas to design for now:** sample-rate mismatch (DAW at 48/96 vs stream at 44.1 → the resampler from §7 must live in the *consumer*, or carry rate in the shared header); DAW buffer-size changes mid-session (ring must tolerate variable read sizes and report xruns); plugin instance lifecycle (multiple instances, instance created before the app is running, app restarted under a live plugin → versioned header with a generation counter and a heartbeat so stale readers detect a zombie writer); reader/writer death leaving a mapping behind (name with PID or clean up on launch); on macOS, sandboxed hosts need the UDP-loopback fallback.
- **v1 design rule:** the jitter buffer must expose decoded PCM through a transport-neutral SPSC ring (`write(frames)` / `read(frames)` over a caller-supplied byte region with a small versioned header). In v1 the region is heap memory inside the app; in v2 the same code maps it from shm or feeds a loopback socket. Do not let the mixer/resampler reach into JUCE types.

---

## 10. GitHub Actions

- **Runner images** (https://github.com/actions/runner-images README, 2026-10-16): `macos-26`/`macos-latest` (arm64; Xcode 26.6 default, CMake 4.4.3, Ninja 1.13.2, Apple clang 21), `macos-26-intel`, `macos-15` (arm64) and `macos-15-intel`, `macos-14` **deprecated** (brownouts through October, removed 2026-11-02, https://github.com/actions/runner-images/issues/13518); `xcode-27` preview. Windows: `windows-2025`/`windows-latest` (Server 2025 build 26100; VS 2022 17.14, MSVC VC.Tools 17.14, CMake 3.31.6, Ninja 1.13.2, LLVM/clang-cl 20.1.8, Windows SDK 10.0.26100), `windows-2025-vs2026`, `windows-2022`, `windows-11-arm`, `windows-11-vs2026-arm`. macOS 26 became GA on 2026-02-26 and `macos-latest` moved to it in June 2026 (https://github.blog/changelog/2026-02-26-macos-26-is-now-generally-available-for-github-hosted-runners/).
- **Cost:** "The use of standard GitHub-hosted runners is free: In public repositories" (https://docs.github.com/en/billing/managing-billing-for-your-products/managing-billing-for-github-actions); larger runners are always billed. Limits: 20 concurrent jobs, **5 concurrent macOS jobs** on the Free plan, 6 h per job (https://docs.github.com/en/actions/reference/limits).
- **Universal build on CI:** configure with `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` on an arm64 runner; run the test binary natively (arm64) and, for the x86_64 slice, under Rosetta (`arch -x86_64 ./tests`) — Rosetta is installed on the arm64 images **[inference; verify on first run]**.
- **Windows core without JUCE:** the core is plain CMake, so `cmake -G Ninja -DCMAKE_CXX_COMPILER=cl` and a second job with `clang-cl` (`-T ClangCL` or `CMAKE_CXX_COMPILER=clang-cl`) both work on the stock image. **ASan**: MSVC has `/fsanitize=address` (VS 2019 16.9+); clang-cl has `-fsanitize=address`. **UBSan**: not in MSVC; Clang's UBSan docs list Windows as supported (https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) but clang-cl's UBSan runtime on Windows is patchy in practice (CLion's docs only list ASan for Windows) **[inference]** — run UBSan (and TSan for the lock-free ring) on macOS clang and on `ubuntu-latest`, which is also the cheapest and fastest runner. Suggested matrix: ubuntu (gcc + clang, ASan/UBSan/TSan, core only), macos-15 + macos-26 (universal app, ASan/UBSan core), windows-2025 (MSVC ASan core; clang-cl build check).

---

## 11. Code signing & notarisation outside the App Store

- **Certificate:** "Developer ID Application" for the `.app` and the `.dmg`; (Developer ID Installer only if you ship a `.pkg`). Sign with `--options runtime --timestamp`; JUCE: `HARDENED_RUNTIME_ENABLED TRUE`.
- **Entitlements:** an output-only audio + network app needs **none**. No `com.apple.security.device.audio-input` (no capture), no `cs.allow-unsigned-executable-memory`/`disable-library-validation` (no JIT, no third-party plugins loaded in v1). Add `com.apple.security.cs.disable-library-validation` only if v2 hosts someone else's code. Local Network privacy is a usage-description string, not an entitlement, on macOS (TN3179: "The multicast entitlement isn't required on macOS").
- **Info.plist:** `NSLocalNetworkUsageDescription`, `NSBonjourServices` (`_raop._tcp`, `_airplay._tcp`, `_dacp._tcp`), `LSUIElement` for the menu-bar app.
- **Workflow:** `xcrun notarytool store-credentials` once; `xcrun notarytool submit TapeIn.dmg --keychain-profile … --wait`; then `xcrun stapler staple TapeIn.app` *before* building the DMG and `xcrun stapler staple TapeIn.dmg` after (so both the mounted app and the downloaded image validate offline); `spctl -a -vv -t install TapeIn.dmg` to check (Apple, https://developer.apple.com/documentation/security/customizing-the-notarization-workflow — page body did not render in my fetch; steps are the standard ones).
- **SMAppService and notarisation:** the header requires code signing for all SMAppService use and notarisation only for apps that *contain LaunchDaemons*; a login item does not need notarisation per se — but a downloaded app is Gatekeeper-blocked without it, and TN3179 says Local Network privacy tracks identity by code signature and "sign it with an Apple-issued code-signing identity". No /Applications requirement is documented for `mainApp`.

---

## Open items to verify during Phase 1
1. Read `libsamplerate/src/src_sinc.c` to confirm `src_process` never allocates (or use `src_callback_read`).
2. Confirm the exact licence headers inside the ASIO SDK you vendor and whether JUCE 9 still needs `JUCE_ASIO=1` + include path.
3. Smoke-test `DnsServiceRegister` with the RAOP TXT set on a Windows 11 VM (TXT visible from a Mac's `dns-sd -B _raop._tcp`).
4. Verify `kSecKeyAlgorithmRSASignatureDigestPKCS1v15Raw` output equals Mbed TLS `MBEDTLS_MD_NONE` output for the same 32-byte challenge (it should be byte-identical; this becomes a unit test if you keep a Security.framework backend).
5. Check JUCE 9.0.4's new CoreAudio device layer against a USB DAC with 44.1/48/96 switching before committing to 9 over 8.0.16.
