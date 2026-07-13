#include <gtest/gtest.h>
#include <Platform/PlatformEncoding.h>

#include <string>

namespace
{
// "한국어" (Korean language) in CP949 bytes...
const std::string KOREAN_CP949 = "\xC7\xD1\xB1\xB9\xBE\xEE";
// ...and the same text in UTF-8.
const std::string KOREAN_UTF8  = "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4";
} // namespace

TEST(PlatformEncodingTest, AsciiPassesThroughUnchanged)
{
	EXPECT_EQ(Cp949ToUtf8("Knight OnLine 1298"), "Knight OnLine 1298");
	EXPECT_EQ(Utf8ToCp949("Knight OnLine 1298"), "Knight OnLine 1298");
}

TEST(PlatformEncodingTest, EmptyStringsAreHandled)
{
	EXPECT_EQ(Cp949ToUtf8(""), "");
	EXPECT_EQ(Utf8ToCp949(""), "");
}

TEST(PlatformEncodingTest, KoreanCp949ConvertsToUtf8)
{
	EXPECT_EQ(Cp949ToUtf8(KOREAN_CP949), KOREAN_UTF8);
}

TEST(PlatformEncodingTest, KoreanUtf8ConvertsToCp949)
{
	EXPECT_EQ(Utf8ToCp949(KOREAN_UTF8), KOREAN_CP949);
}

TEST(PlatformEncodingTest, RoundTripPreservesMixedContent)
{
	const std::string mixedUtf8 = "[Guild] " + KOREAN_UTF8 + " -> ok!";
	EXPECT_EQ(Cp949ToUtf8(Utf8ToCp949(mixedUtf8)), mixedUtf8);
}
