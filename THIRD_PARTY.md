# Third-party components

Every dependency, vendored or fetched, with its license. All are compatible
with AGPL-3.0-or-later. Keep this list in sync with `cmake/Dependencies.cmake`
and `third_party/`.

| Component | Version / commit | License | Use | Where |
|---|---|---|---|---|
| Catch2 | v3.16.1 | BSL-1.0 | Unit tests (not shipped) | FetchContent |
| Mbed TLS | 3.6.7 (release tarball, SHA-256 pinned) | Apache-2.0 OR GPL-2.0-or-later (used under Apache-2.0) | RSA PKCS#1 v1.5 raw signing, RSA-OAEP-SHA1, AES-128-CBC | FetchContent; config in `third_party/mbedtls_config.h` |
| Apple Lossless Audio Codec (reference) | macosforge/alac @ c38887c | Apache-2.0 | ALAC decoding (shipped); encoder used by tests/tools only | `third_party/alac/`, vendored with four validation patches listed in `UPSTREAM` |
| libsamplerate | 0.2.2 | BSD-2-Clause | Sinc resampling with variable ratio (shipped) | FetchContent |
| JUCE | 9.0.4 | AGPL-3.0 (JUCE's open-source option; splash screen disabled as that licence allows) | App layer: UI, audio device I/O, status-bar icon (shipped) | FetchContent, only with `TAPEIN_BUILD_APP` |
| pyatv | 0.18.0 | MIT | Integration-test sender (not shipped) | `tests/integration/requirements.txt`, installed into a venv |
| libraop / cliraop | git HEAD 2026-10 | MIT | Encrypted-path integration sender (not shipped, not vendored) | External binary passed to `tests/integration/raop_play_test.sh` |
