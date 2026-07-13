#include <gtest/gtest.h>

#include <Platform/IconDecoder.h>

#include <filesystem>

namespace fs = std::filesystem;

TEST(IconDecoderTest, DecodesShippedOptionIcon)
{
	const fs::path path = fs::path(OPTION_RES_DIR) / "Option.ico";
	ASSERT_TRUE(fs::exists(path)) << path;

	const DecodedIcon icon = LoadIconFromFile(path);

	ASSERT_TRUE(icon.IsValid()) << path;
	// Option.ico ships 16x16 and 32x32 entries; the decoder should prefer
	// the larger one.
	EXPECT_EQ(icon.width, 32);
	EXPECT_EQ(icon.height, 32);

	int opaque = 0, transparent = 0;
	for (size_t i = 3; i < icon.pixelsRgba.size(); i += 4)
		(icon.pixelsRgba[i] == 0 ? transparent : opaque)++;
	EXPECT_GT(opaque, 0);
}

TEST(IconDecoderTest, RejectsGarbageData)
{
	EXPECT_FALSE(DecodeIconFile({}).IsValid());
	EXPECT_FALSE(DecodeIconFile({0x00, 0x01, 0x02}).IsValid());

	// A cursor (type 2) instead of an icon (type 1) is rejected.
	std::vector<uint8_t> cursor(6 + 16, 0);
	cursor[2] = 2;
	cursor[4] = 1;
	EXPECT_FALSE(DecodeIconFile(cursor).IsValid());
}

TEST(IconDecoderTest, MissingFileReturnsInvalid)
{
	EXPECT_FALSE(LoadIconFromFile("/nonexistent/path/does_not_exist.ico").IsValid());
}
