#include <gtest/gtest.h>

#include <Platform/ProcessLaunch.h>

#include <fstream>

namespace fs = std::filesystem;

namespace
{
class ProcessLaunchTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		root = fs::temp_directory_path() / "openko-processlaunch-test";
		std::error_code ec;
		fs::remove_all(root, ec);
		fs::create_directories(root, ec);
	}

	void TearDown() override
	{
		std::error_code ec;
		fs::remove_all(root, ec);
	}

	// Creates an empty regular file (permissions don't matter to
	// FindSiblingExecutable - it only checks existence/regular-file-ness).
	static void Touch(const fs::path& path)
	{
		std::error_code ec;
		fs::create_directories(path.parent_path(), ec);
		std::ofstream(path).put('\0');
	}

	fs::path root;
};
} // namespace

TEST_F(ProcessLaunchTest, ShellQuoteEscapesQuotesAndBackslashes)
{
	EXPECT_EQ(platform_launch::ShellQuote("plain"), "\"plain\"");
	EXPECT_EQ(platform_launch::ShellQuote("a b"), "\"a b\"");
	EXPECT_EQ(platform_launch::ShellQuote("say \"hi\""), "\"say \\\"hi\\\"\"");
	EXPECT_EQ(platform_launch::ShellQuote("C:\\path"), "\"C:\\\\path\"");
}

TEST_F(ProcessLaunchTest, FindsPlainExecutableNextToDir)
{
	Touch(root / "KnightOnLine");

	const fs::path found = platform_launch::FindSiblingExecutable({ root }, "KnightOnLine");
	EXPECT_EQ(found, root / "KnightOnLine");
}

TEST_F(ProcessLaunchTest, TriesEachSearchDirInOrder)
{
	Touch(root / "second" / "KnightOnLine");

	const fs::path found = platform_launch::FindSiblingExecutable(
		{ root / "first", root / "second" }, "KnightOnLine");
	EXPECT_EQ(found, root / "second" / "KnightOnLine");
}

TEST_F(ProcessLaunchTest, ReturnsEmptyWhenNothingMatches)
{
	const fs::path found = platform_launch::FindSiblingExecutable({ root }, "KnightOnLine");
	EXPECT_TRUE(found.empty());
}

#if defined(__APPLE__)
TEST_F(ProcessLaunchTest, FindsMacOsAppBundleLayout)
{
	// WarFare ships MACOSX_BUNDLE TRUE on macOS: the real binary lives at
	// KnightOnLine.app/Contents/MacOS/KnightOnLine, not directly in the
	// executable's directory - this is the exact layout the "Apply and
	// Execute" button silently failed to find before FindSiblingExecutable
	// grew this branch.
	Touch(root / "KnightOnLine.app" / "Contents" / "MacOS" / "KnightOnLine");

	const fs::path found = platform_launch::FindSiblingExecutable({ root }, "KnightOnLine");
	EXPECT_EQ(found, root / "KnightOnLine.app" / "Contents" / "MacOS" / "KnightOnLine");
}
#endif

TEST_F(ProcessLaunchTest, DoesNotMatchDirectoriesOrWrongName)
{
	std::error_code ec;
	fs::create_directories(root / "KnightOnLine", ec); // a directory, not a file
	Touch(root / "SomethingElse");

	const fs::path found = platform_launch::FindSiblingExecutable({ root }, "KnightOnLine");
	EXPECT_TRUE(found.empty());
}
