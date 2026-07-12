// OptionCore (docs/PORT_POSIX_PLAN.md, client tool ports): pins the
// Option.ini/Server.Ini contract the POSIX Option tool shares with
// OptionDlg.cpp (Windows/MFC) - same defaults, same section/key names, same
// resolution-list filtering/sorting - independent of the ImGui frontend.

#include <gtest/gtest.h>

#include <Client/Option/OptionCore.h>

#include <algorithm>
#include <atomic>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{
fs::path MakeTempIniPath()
{
	static std::atomic<uint32_t> s_counter = 0;
	static const time_t s_time             = time(nullptr);
	return fs::temp_directory_path()
		   / ("OptionCoreTest_" + std::to_string(s_time) + "_" + std::to_string(s_counter++) + ".ini");
}
} // namespace

TEST(OptionCoreTest, LoadOptionsDefaultsMatchWindowsTool)
{
	// A file that doesn't exist yet must yield exactly OptionDlg.cpp's
	// hardcoded defaults (GetPrivateProfileInt's fallback path).
	const fs::path path = MakeTempIniPath();
	std::error_code ec;
	fs::remove(path, ec);

	const option_core::GameOption option = option_core::LoadOptions(path.string());

	EXPECT_EQ(option.iTexLOD_Chr, 0);
	EXPECT_EQ(option.iTexLOD_Shape, 0);
	EXPECT_EQ(option.iTexLOD_Terrain, 0);
	EXPECT_EQ(option.iUseShadow, 1);
	EXPECT_EQ(option.iViewWidth, 1024);
	EXPECT_EQ(option.iViewHeight, 768);
	EXPECT_EQ(option.iViewColorDepth, 16);
	EXPECT_EQ(option.iViewDist, 512);
	EXPECT_EQ(option.iEffectSndDist, 48);
	EXPECT_EQ(option.iEffectCount, 2000);
	EXPECT_TRUE(option.bSoundBgm);
	EXPECT_TRUE(option.bSoundEffect);
	EXPECT_FALSE(option.bSndDuplicated);
	EXPECT_TRUE(option.bWindowCursor);
	EXPECT_FALSE(option.bWindowMode);
	EXPECT_TRUE(option.bEffectVisible);
}

TEST(OptionCoreTest, SaveThenLoadRoundTrips)
{
	const fs::path path = MakeTempIniPath();
	std::error_code ec;
	fs::remove(path, ec);

	option_core::GameOption written;
	written.iTexLOD_Chr     = 1;
	written.iTexLOD_Shape   = 1;
	written.iTexLOD_Terrain = 0;
	written.iUseShadow      = 0;
	written.iViewWidth      = 1920;
	written.iViewHeight     = 1080;
	written.iViewColorDepth = 32;
	written.iViewDist       = 300;
	written.iEffectSndDist  = 25;
	written.iEffectCount    = 1500;
	written.bSndDuplicated  = true;
	written.bSoundBgm       = false;
	written.bSoundEffect    = false;
	written.bWindowCursor   = false;
	written.bWindowMode     = true;
	written.bEffectVisible  = false;

	option_core::SaveOptions(path.string(), written);
	const option_core::GameOption reloaded = option_core::LoadOptions(path.string());

	EXPECT_EQ(reloaded.iTexLOD_Chr, written.iTexLOD_Chr);
	EXPECT_EQ(reloaded.iTexLOD_Shape, written.iTexLOD_Shape);
	EXPECT_EQ(reloaded.iTexLOD_Terrain, written.iTexLOD_Terrain);
	EXPECT_EQ(reloaded.iUseShadow, written.iUseShadow);
	EXPECT_EQ(reloaded.iViewWidth, written.iViewWidth);
	EXPECT_EQ(reloaded.iViewHeight, written.iViewHeight);
	EXPECT_EQ(reloaded.iViewColorDepth, written.iViewColorDepth);
	EXPECT_EQ(reloaded.iViewDist, written.iViewDist);
	EXPECT_EQ(reloaded.iEffectSndDist, written.iEffectSndDist);
	EXPECT_EQ(reloaded.iEffectCount, written.iEffectCount);
	EXPECT_EQ(reloaded.bSndDuplicated, written.bSndDuplicated);
	EXPECT_EQ(reloaded.bSoundBgm, written.bSoundBgm);
	EXPECT_EQ(reloaded.bSoundEffect, written.bSoundEffect);
	EXPECT_EQ(reloaded.bWindowCursor, written.bWindowCursor);
	EXPECT_EQ(reloaded.bWindowMode, written.bWindowMode);
	EXPECT_EQ(reloaded.bEffectVisible, written.bEffectVisible);

	fs::remove(path, ec);
}

TEST(OptionCoreTest, SaveDoesNotDisturbUnrelatedFileContent)
{
	// Option.ini is also read by WarFare (GameOptions.cpp); saving from the
	// Option tool must not clobber keys the game itself owns.
	const fs::path path = MakeTempIniPath();
	{
		std::ofstream out(path);
		out << "[SomeOtherSection]\n";
		out << "CustomKey=CustomValue\n";
	}

	option_core::SaveOptions(path.string(), option_core::GameOption {});

	// Re-open directly (not through OptionCore) to make sure the foreign
	// section survived the rewrite.
	std::ifstream in(path);
	std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	EXPECT_NE(contents.find("[SomeOtherSection]"), std::string::npos);
	EXPECT_NE(contents.find("CustomKey=CustomValue"), std::string::npos);

	std::error_code ec;
	fs::remove(path, ec);
}

TEST(OptionCoreTest, ServerVersionRoundTrips)
{
	const fs::path path = MakeTempIniPath();
	std::error_code ec;
	fs::remove(path, ec);

	EXPECT_EQ(option_core::ReadServerVersion(path.string()), 0);

	option_core::WriteServerVersion(path.string(), 1298);
	EXPECT_EQ(option_core::ReadServerVersion(path.string()), 1298);

	fs::remove(path, ec);
}

TEST(OptionCoreTest, DefaultResolutionsIncludesTheMinimum)
{
	// DefaultResolutions() mirrors the Windows tool's hardcoded literal array
	// verbatim (unsorted, matching source order) - sorting only happens in
	// BuildSupportedResolutions, same as COptionDlg::LoadSupportedResolutions.
	const std::vector<option_core::Resolution> defaults = option_core::DefaultResolutions();
	ASSERT_FALSE(defaults.empty());
	EXPECT_NE(std::find(defaults.begin(), defaults.end(), option_core::MIN_RESOLUTION), defaults.end());
}

TEST(OptionCoreTest, BuildSupportedResolutionsFallsBackToDefaultsWhenNoneDetected)
{
	const std::vector<option_core::Resolution> result = option_core::BuildSupportedResolutions({});

	// Same members as the hardcoded fallback list, but sorted descending.
	std::vector<option_core::Resolution> expected = option_core::DefaultResolutions();
	std::sort(expected.begin(), expected.end(),
		[](const option_core::Resolution& a, const option_core::Resolution& b)
		{ return a.Width > b.Width || (a.Width == b.Width && a.Height > b.Height); });

	EXPECT_EQ(result, expected);
	EXPECT_EQ(result.front(), (option_core::Resolution { 1600, 1200 })); // widest first
}

TEST(OptionCoreTest, BuildSupportedResolutionsFiltersBelowMinimumAndDeduplicates)
{
	const std::vector<option_core::Resolution> detected = {
		{ 800, 600 },   // below MIN_RESOLUTION, filtered out
		{ 1920, 1080 }, //
		{ 1920, 1080 }, // duplicate
		{ 1280, 720 },  // below minimum height (768), filtered out
		{ 2560, 1440 }, //
	};

	const std::vector<option_core::Resolution> result = option_core::BuildSupportedResolutions(detected);

	ASSERT_EQ(result.size(), 2u);
	EXPECT_EQ(result[0], (option_core::Resolution { 2560, 1440 })); // sorted descending
	EXPECT_EQ(result[1], (option_core::Resolution { 1920, 1080 }));
}
