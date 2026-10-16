// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors
//
// Minimal Mbed TLS 3.6 configuration for Tape In: only what the RAOP
// handshake needs (RSA PKCS#1 v1.5 raw signing, RSA-OAEP-SHA1 decryption,
// AES-128-CBC) plus the RNG that RSA blinding requires. No TLS, no X.509,
// no PSA front end.

#pragma once

// Platform
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_ERROR_C

// Big numbers and RSA
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_GENPRIME

// Key parsing (PKCS#1 PEM)
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_OID_C

// Hashes (SHA-1 for OAEP/MGF1; SHA-256 for the DRBG)
#define MBEDTLS_MD_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C

// AES-CBC
#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC

// RNG for RSA blinding
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C
