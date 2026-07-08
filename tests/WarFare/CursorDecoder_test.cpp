#include <gtest/gtest.h>

#include <CursorDecoder.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace
{
// The real cursor files shipped with the client sources.
const fs::path CURSOR_DIR = fs::path(WARFARE_SOURCE_DIR);

struct PixelStats
{
	int opaque      = 0;
	int transparent = 0;
};

PixelStats CountPixels(const DecodedCursor& cursor)
{
	PixelStats stats;
	for (size_t i = 3; i < cursor.pixelsRgba.size(); i += 4)
	{
		if (cursor.pixelsRgba[i] == 0)
			++stats.transparent;
		else
			++stats.opaque;
	}
	return stats;
}
} // namespace

class CursorDecoderTest : public ::testing::TestWithParam<const char*>
{
};

TEST_P(CursorDecoderTest, DecodesShippedCursorFile)
{
	const fs::path path = CURSOR_DIR / GetParam();
	ASSERT_TRUE(fs::exists(path)) << path;

	const DecodedCursor cursor = LoadCursorFromFile(path);

	ASSERT_TRUE(cursor.IsValid()) << path;
	EXPECT_GT(cursor.width, 0);
	EXPECT_LE(cursor.width, 256);
	EXPECT_GT(cursor.height, 0);
	EXPECT_LE(cursor.height, 256);
	EXPECT_GE(cursor.hotspotX, 0);
	EXPECT_LT(cursor.hotspotX, cursor.width);
	EXPECT_GE(cursor.hotspotY, 0);
	EXPECT_LT(cursor.hotspotY, cursor.height);

	// A real cursor has both visible pixels and a transparent surround.
	const PixelStats stats = CountPixels(cursor);
	EXPECT_GT(stats.opaque, 0) << path;
	EXPECT_GT(stats.transparent, 0) << path;
}

INSTANTIATE_TEST_SUITE_P(ShippedCursors, CursorDecoderTest,
	::testing::Values("Cursor_Normal.cur", "Cursor_Attack.cur", "Cursor_Click.cur",
		"cursor_normal1.cur", "cursor_click1.cur", "repair0.cur", "repair1.cur"));

TEST(CursorDecoderNegativeTest, RejectsGarbageData)
{
	EXPECT_FALSE(DecodeCursorFile({}).IsValid());
	EXPECT_FALSE(DecodeCursorFile({0x00, 0x01, 0x02}).IsValid());

	// An icon (type 1) instead of a cursor (type 2) is rejected.
	std::vector<uint8_t> icon(6 + 16, 0);
	icon[2] = 1;
	icon[4] = 1;
	EXPECT_FALSE(DecodeCursorFile(icon).IsValid());
}
