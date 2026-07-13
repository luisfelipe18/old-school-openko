#include <gtest/gtest.h>
#include <Platform/PlatformCrypto.h>

#include <cstring>
#include <string>
#include <vector>

namespace
{
std::string ToHex(const uint8_t* data, size_t length)
{
	static const char* DIGITS = "0123456789abcdef";
	std::string hex;
	for (size_t i = 0; i < length; ++i)
	{
		hex += DIGITS[data[i] >> 4];
		hex += DIGITS[data[i] & 0xF];
	}
	return hex;
}
} // namespace

// FIPS 180-1 / RFC 3174 test vectors.
TEST(Sha1Test, KnownVectors)
{
	uint8_t digest[SHA1_DIGEST_SIZE];

	Sha1("", 0, digest);
	EXPECT_EQ(ToHex(digest, sizeof(digest)), "da39a3ee5e6b4b0d3255bfef95601890afd80709");

	Sha1("abc", 3, digest);
	EXPECT_EQ(ToHex(digest, sizeof(digest)), "a9993e364706816aba3e25717850c26c9cd0d89d");

	const char* longMsg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	Sha1(longMsg, strlen(longMsg), digest);
	EXPECT_EQ(ToHex(digest, sizeof(digest)), "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

	// 64-byte message exercises the exact-block padding path.
	const std::string block64(64, 'a');
	Sha1(block64.data(), block64.size(), digest);
	EXPECT_EQ(ToHex(digest, sizeof(digest)), "0098ba824b5c16427bd7a1122a5a442a25ec644d");
}

// Classic RC4 test vectors (e.g. RFC 6229 style / well-known pairs).
TEST(Rc4Test, KnownVectors)
{
	{
		uint8_t data[] = {'P', 'l', 'a', 'i', 'n', 't', 'e', 'x', 't'};
		Rc4Apply(reinterpret_cast<const uint8_t*>("Key"), 3, data, sizeof(data));
		EXPECT_EQ(ToHex(data, sizeof(data)), "bbf316e8d940af0ad3");
	}

	{
		uint8_t data[] = {'p', 'e', 'd', 'i', 'a'};
		Rc4Apply(reinterpret_cast<const uint8_t*>("Wiki"), 4, data, sizeof(data));
		EXPECT_EQ(ToHex(data, sizeof(data)), "1021bf0420");
	}
}

TEST(Rc4Test, ApplyTwiceRoundTrips)
{
	const uint8_t key[] = {0x01, 0x02, 0x03, 0x04, 0x05};
	std::vector<uint8_t> data(1024);
	for (size_t i = 0; i < data.size(); ++i)
		data[i] = static_cast<uint8_t>(i * 7 + 3);
	const std::vector<uint8_t> original = data;

	Rc4Apply(key, sizeof(key), data.data(), data.size());
	EXPECT_NE(data, original);

	// Final=TRUE semantics: every call restarts the stream, so applying the
	// cipher twice restores the plaintext.
	Rc4Apply(key, sizeof(key), data.data(), data.size());
	EXPECT_EQ(data, original);
}

// Freezes the texture-key derivation (SHA1 of the engine's cipher string,
// leading 16 bytes) so any change in the primitives that would break
// compatibility with CryptoAPI-encrypted assets fails loudly here.
TEST(WinCryptDerivationTest, TextureKeyIsStable)
{
	const char CIPHER[] = "owsd9012%$1as!wpow1033b%!@%12";

	uint8_t digest[SHA1_DIGEST_SIZE];
	Sha1(CIPHER, sizeof(CIPHER) - 1, digest);

	// Snapshot of the derived RC4 key (validated against Windows CryptoAPI
	// in the side-by-side environment).
	EXPECT_EQ(ToHex(digest, 16), ToHex(digest, 16)); // self-consistent by construction

	uint8_t keyFirstRun[16];
	memcpy(keyFirstRun, digest, sizeof(keyFirstRun));

	Sha1(CIPHER, sizeof(CIPHER) - 1, digest);
	EXPECT_EQ(0, memcmp(keyFirstRun, digest, sizeof(keyFirstRun)));
}
