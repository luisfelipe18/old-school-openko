#ifndef _WIN32

#include <gtest/gtest.h>

#include <Platform/GameDataDir.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
// Saves/restores CWD and the OPENKO_GAME_DATA env var around each test,
// since FindGameDataDir reads both as ambient state.
class GameDataDirTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		std::error_code ec;
		savedCwd = fs::current_path(ec);

		if (const char* env = std::getenv("OPENKO_GAME_DATA"))
			savedEnv = env;
		unsetenv("OPENKO_GAME_DATA");

		root = fs::temp_directory_path() / "openko-gamedatadir-test";
		fs::remove_all(root, ec);
		fs::create_directories(root, ec);
	}

	void TearDown() override
	{
		std::error_code ec;
		if (!savedCwd.empty())
			fs::current_path(savedCwd, ec);

		if (savedEnv.empty())
			unsetenv("OPENKO_GAME_DATA");
		else
			setenv("OPENKO_GAME_DATA", savedEnv.c_str(), 1);

		fs::remove_all(root, ec);
	}

	static void Touch(const fs::path& path)
	{
		std::error_code ec;
		fs::create_directories(path.parent_path(), ec);
		std::ofstream(path).put('\0');
	}

	fs::path root;
	fs::path savedCwd;
	std::string savedEnv;
};
} // namespace

TEST_F(GameDataDirTest, LooksLikeGameDataDirNeedsServerIniOrData)
{
	EXPECT_FALSE(LooksLikeGameDataDir(root));

	Touch(root / "Server.Ini");
	EXPECT_TRUE(LooksLikeGameDataDir(root));
}

TEST_F(GameDataDirTest, ExplicitOverrideWinsUnconditionally)
{
	EXPECT_EQ(FindGameDataDir("/definitely/not/a/real/dir"), fs::path("/definitely/not/a/real/dir"));
}

TEST_F(GameDataDirTest, EnvVarBeatsDiscovery)
{
	setenv("OPENKO_GAME_DATA", "/from/the/env", 1);
	EXPECT_EQ(FindGameDataDir(), fs::path("/from/the/env"));
}

TEST_F(GameDataDirTest, FindsAssetsClientNextToCwd)
{
	// The repo/distribution layout: the working directory holds an
	// assets/Client/ folder with the game data in it (binaries live in the
	// same folder in a shipped install).
	Touch(root / "assets" / "Client" / "Server.Ini");

	std::error_code ec;
	fs::current_path(root, ec);
	ASSERT_FALSE(ec);

	const fs::path found = FindGameDataDir();
	EXPECT_EQ(found, fs::weakly_canonical(root / "assets" / "Client", ec));
}

#endif // !_WIN32
