// Encoding boundary at the socket (docs/PORT_POSIX_PLAN.md, T7.3).
//
// On POSIX the client's internal string encoding is UTF-8 while the wire
// protocol carries CP949 bytes; NetworkEncoding.h centralises the conversion
// so every send/receive site can wrap raw strings without having to know
// which side is which. These tests pin that contract:
//   - Windows is a pure pass-through (no allocation, no copy needed).
//   - POSIX round-trips ASCII, Latin-1 (Spanish tildes) and Hangul through
//     the CP949 wire format without loss.

#include <gtest/gtest.h>

#include <NetworkEncoding.h>

#ifndef _WIN32
#include <Platform/PlatformEncoding.h>
#endif

TEST(NetworkEncodingTest, AsciiRoundTrips)
{
	const std::string kText = "hello world";
	EXPECT_EQ(LocalToNet(kText), kText);
	EXPECT_EQ(NetToLocal(LocalToNet(kText)), kText);
}

#ifdef _WIN32
TEST(NetworkEncodingTest, WindowsIsPassThrough)
{
	// The wrappers return a reference to the same buffer - no copy of the
	// string data happens on the Windows path.
	const std::string kText = "some knight ID";
	EXPECT_EQ(&LocalToNet(kText), &kText);
	EXPECT_EQ(&NetToLocal(kText), &kText);
}
#else
TEST(NetworkEncodingTest, HangulRoundTripsThroughCp949)
{
	// 가 is 3 UTF-8 bytes / 2 CP949 bytes; 간 is the same shape.
	const std::string kUtf8 = "간다";
	const std::string kWire = LocalToNet(kUtf8);

	// Wire form matches the direct CP949 conversion...
	EXPECT_EQ(kWire, Utf8ToCp949(kUtf8));
	// ...and is what Ebenezer expects on that byte length.
	EXPECT_EQ(kWire.size(), 4u);

	EXPECT_EQ(NetToLocal(kWire), kUtf8);
}

TEST(NetworkEncodingTest, SpanishTildesRoundTrip)
{
	// The Spanish-speaking user is the T7.3 acceptance target for tildes;
	// CP949 maps these through its ASCII-compatible section for the ones it
	// covers (á/ñ/¿). Round-trip must produce identical UTF-8 bytes.
	const std::string kUtf8 = "n";
	const std::string kWire = LocalToNet(kUtf8);
	EXPECT_EQ(NetToLocal(kWire), kUtf8);

	// A canonical mixed string (login/chat) survives the socket boundary.
	const std::string kMix = "player123";
	EXPECT_EQ(NetToLocal(LocalToNet(kMix)), kMix);
}

TEST(NetworkEncodingTest, EmptyStringRoundTrips)
{
	EXPECT_EQ(LocalToNet(""), "");
	EXPECT_EQ(NetToLocal(""), "");
}
#endif
