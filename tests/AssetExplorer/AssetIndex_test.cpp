#include <gtest/gtest.h>

#include <AssetIndex.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace assetexplorer;

namespace
{
// Builds a throwaway asset tree in a unique temp dir; removed by the fixture.
class AssetIndexTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		root = fs::temp_directory_path()
			 / fs::path("openko_assetidx_" + std::to_string(::testing::UnitTest::GetInstance()
					->random_seed()) + std::to_string(reinterpret_cast<uintptr_t>(this)));
		fs::create_directories(root / "mob");
		fs::create_directories(root / "item");
		fs::create_directories(root / "tile");
		fs::create_directories(root / "docs");

		Touch(root / "mob" / "orc.n3chr");
		Touch(root / "mob" / "orc.dxt");
		Touch(root / "item" / "sword.n3cplug");
		Touch(root / "tile" / "grass.dxt");
		Touch(root / "tile" / "GRASS.TGA"); // upper-case ext
		Touch(root / "fireball.n3fxbundle");
		Touch(root / "docs" / "readme.txt"); // non-asset
	}

	void TearDown() override
	{
		std::error_code ec;
		fs::remove_all(root, ec);
	}

	static void Touch(const fs::path& p, const char* bytes = "x")
	{
		std::ofstream(p, std::ios::binary) << bytes;
	}

	fs::path root;
};
} // namespace

TEST_F(AssetIndexTest, ScanSkipsUnknownByDefault)
{
	AssetIndex idx;
	const std::size_t n = idx.Scan(root);
	EXPECT_EQ(n, 6u); // the 6 assets, readme.txt excluded
	EXPECT_EQ(idx.Entries().size(), 6u);
	EXPECT_EQ(idx.Root(), root);
}

TEST_F(AssetIndexTest, ScanCanIncludeUnknown)
{
	AssetIndex idx;
	const std::size_t n = idx.Scan(root, /*includeUnknown=*/true);
	EXPECT_EQ(n, 7u); // includes readme.txt
}

TEST_F(AssetIndexTest, EntriesAreSortedByRelativePathWithForwardSlashes)
{
	AssetIndex idx;
	idx.Scan(root);
	ASSERT_FALSE(idx.Entries().empty());
	for (const auto& e : idx.Entries())
		EXPECT_EQ(e.relativePath.find('\\'), std::string::npos);

	for (std::size_t i = 1; i < idx.Entries().size(); ++i)
		EXPECT_LE(idx.Entries()[i - 1].relativePath, idx.Entries()[i].relativePath);
}

TEST_F(AssetIndexTest, CountsByCategory)
{
	AssetIndex idx;
	idx.Scan(root);
	const auto counts = idx.CountsByCategory();
	// 3 textures: orc.dxt, grass.dxt, GRASS.TGA
	EXPECT_EQ(counts[static_cast<std::size_t>(AssetCategory::Texture)], 3u);
	// 2 characters: orc.n3chr, sword.n3cplug
	EXPECT_EQ(counts[static_cast<std::size_t>(AssetCategory::Character)], 2u);
	// 1 effect: fireball.n3fxbundle
	EXPECT_EQ(counts[static_cast<std::size_t>(AssetCategory::Effect)], 1u);
}

TEST_F(AssetIndexTest, FilterByCategoryMask)
{
	AssetIndex idx;
	idx.Scan(root);

	const unsigned texMask = CategoryBit(AssetCategory::Texture);
	const auto tex = idx.Filter("", texMask);
	EXPECT_EQ(tex.size(), 3u);
	for (std::size_t i : tex)
		EXPECT_EQ(idx.Entries()[i].category, AssetCategory::Texture);

	// mask 0 matches everything
	EXPECT_EQ(idx.Filter("", 0u).size(), idx.Entries().size());

	// combined mask
	const unsigned combo =
		CategoryBit(AssetCategory::Character) | CategoryBit(AssetCategory::Effect);
	EXPECT_EQ(idx.Filter("", combo).size(), 3u);
}

TEST_F(AssetIndexTest, FilterByTextIsCaseInsensitiveSubstring)
{
	AssetIndex idx;
	idx.Scan(root);

	EXPECT_EQ(idx.Filter("orc", 0u).size(), 2u);     // orc.n3chr + orc.dxt
	EXPECT_EQ(idx.Filter("ORC", 0u).size(), 2u);     // case-insensitive
	EXPECT_EQ(idx.Filter("mob/", 0u).size(), 2u);    // path segment
	EXPECT_EQ(idx.Filter("nomatch", 0u).size(), 0u);

	// text + category combined: "grass" restricted to textures
	EXPECT_EQ(idx.Filter("grass", CategoryBit(AssetCategory::Texture)).size(), 2u);
}
