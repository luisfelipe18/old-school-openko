#include <gtest/gtest.h>
#include <FileIO/PathResolver.h>

#include <atomic>
#include <ctime>
#include <fstream>

namespace fs = std::filesystem;

class PathResolverTest : public ::testing::Test
{
protected:
	fs::path _rootPath;

	void SetUp() override
	{
		static std::atomic<uint32_t> s_testCounter = 0;
		static const time_t s_time                 = time(nullptr);

		std::string dirName = "PathResolverTest_" + std::to_string(s_time) + "_"
							  + std::to_string(s_testCounter++);

		_rootPath = fs::temp_directory_path() / dirName;
		fs::create_directories(_rootPath / "UI" / "SubDir");

		std::ofstream(_rootPath / "UI" / "LoginIntro.txt") << "test";
		std::ofstream(_rootPath / "UI" / "SubDir" / "Nested.DAT") << "test";
	}

	void TearDown() override
	{
		std::error_code ec;
		fs::remove_all(_rootPath, ec);
	}
};

TEST_F(PathResolverTest, ExactPathIsReturnedUnchanged)
{
	const fs::path exact = _rootPath / "UI" / "LoginIntro.txt";
	EXPECT_EQ(ResolveCaseInsensitivePath(exact), exact);
}

TEST_F(PathResolverTest, MismatchedCaseResolvesToRealFile)
{
	const fs::path requested = _rootPath / "ui" / "loginintro.txt";
	const fs::path expected  = _rootPath / "UI" / "LoginIntro.txt";

	const fs::path resolved  = ResolveCaseInsensitivePath(requested);

	EXPECT_TRUE(fs::exists(resolved));
	EXPECT_EQ(resolved, expected);
}

TEST_F(PathResolverTest, MismatchedCaseResolvesAcrossMultipleComponents)
{
	const fs::path requested = _rootPath / "ui" / "subdir" / "nested.dat";

	const fs::path resolved  = ResolveCaseInsensitivePath(requested);

	EXPECT_TRUE(fs::exists(resolved));
	EXPECT_EQ(resolved, _rootPath / "UI" / "SubDir" / "Nested.DAT");
}

TEST_F(PathResolverTest, MissingFileIsReturnedAsGivenSoOpenFailsNormally)
{
	const fs::path requested = _rootPath / "ui" / "doesnotexist.txt";

	const fs::path resolved  = ResolveCaseInsensitivePath(requested);

	EXPECT_FALSE(fs::exists(resolved));
}

#ifndef _WIN32
TEST_F(PathResolverTest, BackslashSeparatorsAreNormalized)
{
	const std::string requested = _rootPath.string() + "/ui\\subdir\\nested.dat";

	const fs::path resolved     = ResolveCaseInsensitivePath(requested);

	EXPECT_TRUE(fs::exists(resolved));
	EXPECT_EQ(resolved, _rootPath / "UI" / "SubDir" / "Nested.DAT");
}
#endif
