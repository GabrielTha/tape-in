// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Tape In contributors
//
// Test vectors were generated with the system OpenSSL (LibreSSL 3.3.6) against
// the embedded RAOP key written to key.pem:
//   blob4.bin = bytes 00..0f ‖ 192.168.1.10 ‖ 0a0b0c0d0e0f ‖ 6 zero bytes (32 bytes)
//   openssl pkeyutl -sign -inkey key.pem -pkeyopt rsa_padding_mode:pkcs1 -in blob4.bin
//   blob6.bin = bytes 00..0f ‖ fe80::1 (16 bytes) ‖ 0a0b0c0d0e0f (38 bytes)
//   openssl pkeyutl -sign -inkey key.pem -pkeyopt rsa_padding_mode:pkcs1 -in blob6.bin
//   openssl pkeyutl -encrypt -pubin -inkey pub.pem -pkeyopt rsa_padding_mode:oaep -in aeskey.bin
//     (aeskey.bin = 00112233445566778899aabbccddeeff)
//   openssl enc -aes-128-cbc -nopad -K 00112233445566778899aabbccddeeff \
//     -iv 0f0e0d0c0b0a09080706050403020100 -in plain32.bin   (plain32.bin = bytes 00..1f)

#include "tapein/core/crypto/aes_cbc.h"
#include "tapein/core/crypto/apple_challenge.h"
#include "tapein/core/crypto/raop_key.h"
#include "tapein/core/util/base64.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace tapein::core;
using namespace tapein::core::crypto;

namespace {

std::vector<uint8_t> fromHex(std::string_view hex)
{
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoul(std::string(hex.substr(i, 2)), nullptr, 16)));
    }
    return out;
}

constexpr std::string_view kSig4 =
    "5478c212ed6a4b1ebfb79d60f012617941f4e65c8eaf23a78c32665916d7608e761b3fa1f440d3494b8d38ba618549181da97619f1cab321457943c0b693fea1"
    "3204e1b04b3858d97380e66df95b32dcc9e3765c7a8cae81ae4ee2cc4d204d85fa2ef45c3583d35b87837323602140531b927aeb9b018652c1e577ea762e1ccb"
    "400532ed0c21705925394fab5214590f75f0a9d5200f41897f5bb8196206280bedb6747f5638bd7d4dabba8031db74fc2ccd686c1175e4d24e8fe627713fbfb8"
    "07278f5b6b5e60f83d648bf1ce1f529d0a6bcff7f05235cf0d83859d13cfcf25664812b4aafbe53d9aa8d5b811d9ae822dfd4369c6ca56049d1d0528f367859e";

constexpr std::string_view kSig6 =
    "40345fc079810c9b21d4e5c1cb08935aefbd036511486ab075465387233c1ed6aa1876a53338450ec5aebd82bfe67070a6b3d3ccf962864f0dfdfc9099f9877a"
    "1296f5af49174627701b4edc337c41e3f589402577783871e602f7b2c5e0407fe5dc693b619a34595988f08b4c31ef685ed5afc2e2a296b14ce958a82c0f60ee"
    "fe0f1d22d460ceddb82ba7f87aba970e288740dcebb25baac803a907eea22a178d72c0f83cc4dcb1c0f0086e69e080f74167de08aca060cd7557749a61acfce3"
    "70792b663d136a861cdbc43c07690c0aff9e180451f0bc2d75c1a40c8302d206e73ca7483f94891998bddc0be95120266f57fc06320af7a9d35a8ae03669e305";

constexpr std::string_view kOaep =
    "acae14f1e9957b48088383f56948288170abd7f9381011a9ed44d99e037475cdbcfe693955566af07d6ef93702d6ea4cb4a8087b5f70bc288062ced24835a4f2"
    "67ad1276c608cdd7e738064e4e77d5bdfa3ed35e60ca80078faec42acbe6cc7d23cd0b69bd57e96fc26440bcf81276d706db6c0027f4b7388ea96968525c34b6"
    "6af8a269a0a9cb420394b9b16698bc09cdc70b2032c36076f10a1e8cb77b2933c11190569347a2321831017f377409270f0e716d926ed34015925c37e19c0553"
    "ed7561eb6ad8f3be4a5a71105444aae76c56d33b2fc7927ed08b1d6b3165608731e4f385637de2c02fef42ad5fa0e6c220162c0a08196f5fee321d32d2814452";

constexpr std::string_view kCbc32 = "8fe813763a7e66ffb81785f83e110cef014c408d594a4d770b72b7ca8534ee33";

const HardwareId kHwid{0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
const std::array<uint8_t, 4> kIpv4{192, 168, 1, 10};
const std::array<uint8_t, 16> kIpv6{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};

// bytes 00..0f, base64 with padding: "AAECAwQFBgcICQoLDA0ODw=="
constexpr std::string_view kChallengePadded = "AAECAwQFBgcICQoLDA0ODw==";
constexpr std::string_view kChallengeUnpadded = "AAECAwQFBgcICQoLDA0ODw";

std::shared_ptr<RaopRsaKey> loadKey()
{
    auto r = RaopRsaKey::load();
    if (!r.ok()) { FAIL(r.error().message); }
    return r.value();
}

} // namespace

TEST_CASE("key loads and has 256-byte modulus")
{
    const auto key = loadKey();
    REQUIRE(key->modulusBytes() == 256);
}

TEST_CASE("challenge ipv4 vector")
{
    const auto key = loadKey();
    const auto r = appleChallengeResponse(*key, kChallengePadded, kIpv4, kHwid);
    if (!r.ok()) { FAIL(r.error().message); }
    const auto expected = base64Encode(fromHex(kSig4), false);
    REQUIRE(r.value() == expected);
    REQUIRE(r.value().back() != '=');
}

TEST_CASE("challenge ipv6 vector")
{
    const auto key = loadKey();
    const auto r = appleChallengeResponse(*key, kChallengeUnpadded, kIpv6, kHwid);
    if (!r.ok()) { FAIL(r.error().message); }
    REQUIRE(r.value() == base64Encode(fromHex(kSig6), false));
}

TEST_CASE("base64 without padding in challenge is accepted and gives the same response")
{
    const auto key = loadKey();
    const auto a = appleChallengeResponse(*key, kChallengePadded, kIpv4, kHwid);
    const auto b = appleChallengeResponse(*key, kChallengeUnpadded, kIpv4, kHwid);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(a.value() == b.value());
}

TEST_CASE("challenge longer than 16 bytes or malformed is rejected")
{
    const auto key = loadKey();
    const std::vector<uint8_t> seventeen(17, 0x42);
    REQUIRE_FALSE(appleChallengeResponse(*key, base64Encode(seventeen, true), kIpv4, kHwid).ok());
    REQUIRE_FALSE(appleChallengeResponse(*key, "not base64!!", kIpv4, kHwid).ok());
    const std::array<uint8_t, 5> badAddress{1, 2, 3, 4, 5};
    REQUIRE_FALSE(appleChallengeResponse(*key, kChallengePadded, badAddress, kHwid).ok());
}

TEST_CASE("oaep unwrap vector")
{
    const auto key = loadKey();
    const auto r = key->decryptOaepSha1(fromHex(kOaep));
    if (!r.ok()) { FAIL(r.error().message); }
    REQUIRE(r.value() == fromHex("00112233445566778899aabbccddeeff"));
}

TEST_CASE("oaep encrypt then decrypt round trip")
{
    const auto key = loadKey();
    const auto plain = fromHex("ffeeddccbbaa99887766554433221100");
    const auto cipher = key->encryptOaepSha1(plain);
    REQUIRE(cipher.ok());
    REQUIRE(cipher.value().size() == 256);
    const auto back = key->decryptOaepSha1(cipher.value());
    REQUIRE(back.ok());
    REQUIRE(back.value() == plain);
}

TEST_CASE("oaep rejects wrong length and corrupted input")
{
    const auto key = loadKey();
    const std::vector<uint8_t> shortInput(255, 0x01);
    REQUIRE_FALSE(key->decryptOaepSha1(shortInput).ok());
    auto corrupted = fromHex(kOaep);
    corrupted[10] ^= 0xff;
    REQUIRE_FALSE(key->decryptOaepSha1(corrupted).ok());
}

TEST_CASE("aes cbc decrypt leaves tail bytes and resets iv per packet")
{
    const auto keyBytes = fromHex("00112233445566778899aabbccddeeff");
    const auto ivBytes = fromHex("0f0e0d0c0b0a09080706050403020100");
    AesCbcDecryptor dec(std::span<const uint8_t, 16>(keyBytes.data(), 16), std::span<const uint8_t, 16>(ivBytes.data(), 16));

    std::vector<uint8_t> packet = fromHex(kCbc32);
    packet.insert(packet.end(), 8, 0xAA);
    REQUIRE(dec.decryptPacketInPlace(packet));
    for (size_t i = 0; i < 32; ++i) {
        REQUIRE(packet[i] == static_cast<uint8_t>(i));
    }
    for (size_t i = 32; i < 40; ++i) {
        REQUIRE(packet[i] == 0xAA);
    }

    // Second packet with the same ciphertext must decrypt identically (IV reset).
    std::vector<uint8_t> again = fromHex(kCbc32);
    REQUIRE(dec.decryptPacketInPlace(again));
    REQUIRE(again[0] == 0x00);
    REQUIRE(again[31] == 0x1f);
}

TEST_CASE("aes cbc rejects payload shorter than one block")
{
    const auto keyBytes = fromHex("00112233445566778899aabbccddeeff");
    const auto ivBytes = fromHex("0f0e0d0c0b0a09080706050403020100");
    AesCbcDecryptor dec(std::span<const uint8_t, 16>(keyBytes.data(), 16), std::span<const uint8_t, 16>(ivBytes.data(), 16));
    std::vector<uint8_t> tiny(15, 0);
    REQUIRE_FALSE(dec.decryptPacketInPlace(tiny));
}
