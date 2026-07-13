#include "PlatformCrypto.h"

#include <cstring>

// --- SHA-1 (FIPS 180-1) -------------------------------------------------------

namespace
{
inline uint32_t RotateLeft(uint32_t value, int bits)
{
	return (value << bits) | (value >> (32 - bits));
}

void Sha1ProcessBlock(uint32_t state[5], const uint8_t block[64])
{
	uint32_t w[80];
	for (int i = 0; i < 16; ++i)
	{
		w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16)
			   | (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
	}
	for (int i = 16; i < 80; ++i)
		w[i] = RotateLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

	uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];

	for (int i = 0; i < 80; ++i)
	{
		uint32_t f = 0, k = 0;
		if (i < 20)
		{
			f = (b & c) | ((~b) & d);
			k = 0x5A827999;
		}
		else if (i < 40)
		{
			f = b ^ c ^ d;
			k = 0x6ED9EBA1;
		}
		else if (i < 60)
		{
			f = (b & c) | (b & d) | (c & d);
			k = 0x8F1BBCDC;
		}
		else
		{
			f = b ^ c ^ d;
			k = 0xCA62C1D6;
		}

		const uint32_t temp = RotateLeft(a, 5) + f + e + k + w[i];
		e = d;
		d = c;
		c = RotateLeft(b, 30);
		b = a;
		a = temp;
	}

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
}
} // namespace

void Sha1(const void* data, size_t length, uint8_t digest[SHA1_DIGEST_SIZE])
{
	uint32_t state[5]    = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

	const uint8_t* bytes = static_cast<const uint8_t*>(data);
	size_t remaining     = length;

	while (remaining >= 64)
	{
		Sha1ProcessBlock(state, bytes);
		bytes     += 64;
		remaining -= 64;
	}

	// Final block(s): message + 0x80 padding + 64-bit big-endian bit length.
	uint8_t block[64] = {};
	memcpy(block, bytes, remaining);
	block[remaining] = 0x80;

	if (remaining >= 56)
	{
		Sha1ProcessBlock(state, block);
		memset(block, 0, sizeof(block));
	}

	const uint64_t bitLength = uint64_t(length) * 8;
	for (int i = 0; i < 8; ++i)
		block[56 + i] = static_cast<uint8_t>(bitLength >> (56 - i * 8));
	Sha1ProcessBlock(state, block);

	for (int i = 0; i < 5; ++i)
	{
		digest[i * 4]     = static_cast<uint8_t>(state[i] >> 24);
		digest[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
		digest[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
		digest[i * 4 + 3] = static_cast<uint8_t>(state[i]);
	}
}

// --- RC4 ----------------------------------------------------------------------

void Rc4Apply(const uint8_t* key, size_t keyLength, void* data, size_t dataLength)
{
	uint8_t s[256];
	for (int i = 0; i < 256; ++i)
		s[i] = static_cast<uint8_t>(i);

	uint8_t j = 0;
	for (int i = 0; i < 256; ++i)
	{
		j = static_cast<uint8_t>(j + s[i] + key[i % keyLength]);
		const uint8_t tmp = s[i];
		s[i]              = s[j];
		s[j]              = tmp;
	}

	uint8_t* bytes = static_cast<uint8_t*>(data);
	uint8_t i = 0;
	j         = 0;
	for (size_t n = 0; n < dataLength; ++n)
	{
		i                 = static_cast<uint8_t>(i + 1);
		j                 = static_cast<uint8_t>(j + s[i]);
		const uint8_t tmp = s[i];
		s[i]              = s[j];
		s[j]              = tmp;
		bytes[n]         ^= s[static_cast<uint8_t>(s[i] + s[j])];
	}
}
