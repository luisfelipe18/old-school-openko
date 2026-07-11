// Platform path helpers (docs/PORT_POSIX_PLAN.md, F8): the client resolves
// executable-relative resources through GetExecutableDir() and writes logs
// under GetUserConfigDir(). These tests pin those contracts on the platforms
// that use them; the shims are no-ops on Windows.

#include <gtest/gtest.h>

#include <Platform/PlatformPaths.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#ifdef _WIN32

TEST(PlatformPathsTest, ShimsAreEmptyOnWindows)
{
	// On Windows the "run next to the exe" convention makes both helpers
	// meaningless; both must yield an empty path.
	EXPECT_TRUE(GetExecutableDir().empty());
	EXPECT_TRUE(GetUserConfigDir().empty());
}

#else

TEST(PlatformPathsTest, GetExecutableDirPointsAtRunningBinary)
{
	// The test executable itself is a running binary, so its parent directory
	// must contain the file that produced this process.
	std::error_code ec;
	const std::filesystem::path dir = GetExecutableDir();
	ASSERT_FALSE(dir.empty());
	EXPECT_TRUE(std::filesystem::is_directory(dir, ec));

	// argv[0] equivalent: /proc/self/exe or _NSGetExecutablePath resolves to
	// a file that lives in that directory.
#if defined(__linux__)
	auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
	ASSERT_FALSE(ec);
	EXPECT_EQ(exe.parent_path(), dir);
#endif
}

TEST(PlatformPathsTest, UserConfigDirHonoursXdgOnLinux)
{
#if defined(__APPLE__)
	// macOS uses ~/Library/Application Support/OpenKO and ignores XDG_CONFIG_HOME.
	setenv("HOME", "/tmp/openko-fake-home", 1);
	EXPECT_EQ(GetUserConfigDir(),
		std::filesystem::path("/tmp/openko-fake-home/Library/Application Support/OpenKO"));
#else
	// Linux picks XDG_CONFIG_HOME first, then HOME/.config, then empty.
	setenv("XDG_CONFIG_HOME", "/tmp/openko-xdg", 1);
	EXPECT_EQ(GetUserConfigDir(), std::filesystem::path("/tmp/openko-xdg/openko"));

	unsetenv("XDG_CONFIG_HOME");
	setenv("HOME", "/tmp/openko-fake-home", 1);
	EXPECT_EQ(GetUserConfigDir(),
		std::filesystem::path("/tmp/openko-fake-home/.config/openko"));

	unsetenv("HOME");
	EXPECT_TRUE(GetUserConfigDir().empty());
#endif
}

#endif // _WIN32
