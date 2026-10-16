# Legal notes

This file records the legal and intellectual-property questions the project
has considered, and the decisions taken. It is not legal advice.

## Trademarks

"AirPlay" is a trademark of Apple Inc., registered in the U.S. and other
countries and regions. Tape In uses the word only descriptively ("works
with AirPlay"), never in the product name, bundle identifiers, icons or
badges, and always with the attribution and the no-affiliation notice. Tape
In is not part of Apple's MFi program and does not use the "Works with
Apple AirPlay" badge.

## The RAOP RSA private key

### What it is

AirPlay-1 (RAOP) senders authenticate a receiver by sending an
`Apple-Challenge` and expecting an `Apple-Response` signed with a specific
2048-bit RSA private key, and they wrap the per-session AES key with the
matching public key. A receiver that does not hold the private key cannot
be used by iOS devices. The key is the same for every receiver.

### Provenance

The key was read out of the firmware of an AirPort Express by James Laird
and published with the original shairport in April 2011. Since then it has
been embedded, unchanged, in shairport-sync (packaged in Debian main,
Fedora, Homebrew, Arch and OpenWrt for over a decade), AirBass,
openairplay/airplay2-receiver, AirPlayWin, ESPHome-AirPlay and, as far as
can be inferred from its binary, in commercial products such as auxCord.
No lawsuit, DMCA notice or cease-and-desist against any of these projects
or distributions is publicly known. Apple's only documented action in this
area was a 2012 App Store removal of an iOS receiver app.

shairplay (Juho Vähä-Herttua) chose not to bundle the key "for possible
legal reasons" and requires the user to supply `airport.key`.

### Decision (2026-10-16)

The maintainer delegated the call; the project embeds the key, taking the
same position as the Linux distributions that package shairport-sync. The
key material lives in exactly one place, `core/src/crypto/raop_key.cpp`
(`raopPrivateKeyPem()`), so that switching to a user-supplied key file is a
change to one function. The PEM in that file was generated mechanically from
the published key and is verified by the unit tests in
`tests/unit/crypto_test.cpp` against vectors produced with OpenSSL.

### What this means for users

Using Tape In to receive audio from your own devices on your own network is
the same thing a decade of open-source receivers have done. If you are in a
jurisdiction or organisation where that is a concern, build Tape In with
the key replaced by a file you supply (see `raop_key.cpp`).

## Reference code

Tape In's core was written from scratch. Protocol understanding came from
reading shairport-sync (MIT), AirBass (Apache-2.0), UxPlay (LGPL-2.1+/GPLv3)
and openairplay/airplay2-receiver (no license, read only). No code was
copied from any of them. Vendored third-party code is listed in
`THIRD_PARTY.md` with its license.

## AGPL §13

An iPhone streaming to Tape In arguably "interacts with it remotely through
a computer network". To make the source offer unmissable, Tape In puts the
repository URL in the About window, in the `src=` key of its Bonjour TXT
record and in this repository's README.
