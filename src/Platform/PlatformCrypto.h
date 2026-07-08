#ifndef PLATFORM_PLATFORMCRYPTO_H
#define PLATFORM_PLATFORMCRYPTO_H

#pragma once

#include <cstddef>
#include <cstdint>

// Minimal crypto primitives for the POSIX port of CWinCrypt (encrypted
// texture support). The Windows client derives its texture key with
// CryptoAPI: CryptDeriveKey(CALG_RC4, SHA1(cipher), 128-bit). For RC4 keys no
// longer than the hash, CryptDeriveKey's key material is simply the leading
// bytes of the hash value, so SHA-1 + RC4 reproduce it exactly.

inline constexpr size_t SHA1_DIGEST_SIZE = 20;

/// \brief Computes the SHA-1 digest of a buffer.
void Sha1(const void* data, size_t length, uint8_t digest[SHA1_DIGEST_SIZE]);

/// \brief RC4 stream cipher, applied in place from stream position zero.
///
/// Matches CryptDecrypt(..., Final=TRUE, ...) semantics for stream ciphers:
/// each call restarts the key stream (the engine decrypts whole buffers in
/// one call).
void Rc4Apply(const uint8_t* key, size_t keyLength, void* data, size_t dataLength);

#endif // PLATFORM_PLATFORMCRYPTO_H
