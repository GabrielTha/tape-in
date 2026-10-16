# Third-party components

Every dependency, vendored or fetched, with its license. All are compatible
with AGPL-3.0-or-later. Keep this list in sync with `cmake/Dependencies.cmake`
and `third_party/`.

| Component | Version / commit | License | Use | Where |
|---|---|---|---|---|
| Catch2 | v3.16.1 | BSL-1.0 | Unit tests (not shipped) | FetchContent |
| Mbed TLS | 3.6.7 (release tarball, SHA-256 pinned) | Apache-2.0 OR GPL-2.0-or-later (used under Apache-2.0) | RSA PKCS#1 v1.5 raw signing, RSA-OAEP-SHA1, AES-128-CBC | FetchContent; config in `third_party/mbedtls_config.h` |
