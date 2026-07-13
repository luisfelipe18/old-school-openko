// PlatformIni.h round-trip (docs/PORT_POSIX_PLAN.md, client tool ports):
// WritePrivateProfileString must update an existing key in place, append a
// missing key to its section, create the section if it doesn't exist yet,
// and leave unrelated sections/keys untouched - this is the file COptionDlg
// reads/writes on Windows via the real Win32 API, now shared by the POSIX
// Option port.

#ifndef _WIN32

#include <gtest/gtest.h>

#include <Platform/PlatformIni.h>

#include <atomic>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
fs::path MakeTempIniPath()
{
	static std::atomic<uint32_t> s_counter = 0;
	static const time_t s_time             = time(nullptr);
	return fs::temp_directory_path()
		   / ("PlatformIniTest_" + std::to_string(s_time) + "_" + std::to_string(s_counter++) + ".ini");
}

std::string ReadWholeFile(const fs::path& path)
{
	std::ifstream in(path);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
} // namespace

TEST(PlatformIniTest, WriteCreatesFileAndSection)
{
	const fs::path path = MakeTempIniPath();
	std::error_code ec;
	fs::remove(path, ec);

	EXPECT_TRUE(WritePrivateProfileString("ViewPort", "Width", "1024", path.string().c_str()));
	EXPECT_EQ(GetPrivateProfileInt("ViewPort", "Width", -1, path.string().c_str()), 1024);

	fs::remove(path, ec);
}

TEST(PlatformIniTest, WriteUpdatesExistingKeyInPlace)
{
	const fs::path path = MakeTempIniPath();
	{
		std::ofstream out(path);
		out << "[ViewPort]\n";
		out << "Width=800\n";
		out << "Height=600\n";
		out << "\n";
		out << "[Sound]\n";
		out << "Bgm=1\n";
	}

	EXPECT_TRUE(WritePrivateProfileString("ViewPort", "Width", "1920", path.string().c_str()));

	EXPECT_EQ(GetPrivateProfileInt("ViewPort", "Width", -1, path.string().c_str()), 1920);
	// Untouched keys/sections survive.
	EXPECT_EQ(GetPrivateProfileInt("ViewPort", "Height", -1, path.string().c_str()), 600);
	EXPECT_EQ(GetPrivateProfileInt("Sound", "Bgm", -1, path.string().c_str()), 1);

	std::error_code ec;
	fs::remove(path, ec);
}

TEST(PlatformIniTest, WriteAppendsMissingKeyToExistingSection)
{
	const fs::path path = MakeTempIniPath();
	{
		std::ofstream out(path);
		out << "[ViewPort]\n";
		out << "Width=800\n";
		out << "\n";
		out << "[Sound]\n";
		out << "Bgm=1\n";
	}

	EXPECT_TRUE(WritePrivateProfileString("ViewPort", "Height", "600", path.string().c_str()));

	EXPECT_EQ(GetPrivateProfileInt("ViewPort", "Width", -1, path.string().c_str()), 800);
	EXPECT_EQ(GetPrivateProfileInt("ViewPort", "Height", -1, path.string().c_str()), 600);
	// The following section (and its own keys) must not be disturbed.
	EXPECT_EQ(GetPrivateProfileInt("Sound", "Bgm", -1, path.string().c_str()), 1);

	std::error_code ec;
	fs::remove(path, ec);
}

TEST(PlatformIniTest, WriteAppendsNewSectionAtEndOfFile)
{
	const fs::path path = MakeTempIniPath();
	{
		std::ofstream out(path);
		out << "[ViewPort]\n";
		out << "Width=800\n";
	}

	EXPECT_TRUE(WritePrivateProfileString("Cursor", "WindowCursor", "1", path.string().c_str()));

	EXPECT_EQ(GetPrivateProfileInt("ViewPort", "Width", -1, path.string().c_str()), 800);
	EXPECT_EQ(GetPrivateProfileInt("Cursor", "WindowCursor", -1, path.string().c_str()), 1);

	std::error_code ec;
	fs::remove(path, ec);
}

TEST(PlatformIniTest, RoundTripsThroughMultipleWrites)
{
	const fs::path path = MakeTempIniPath();
	std::error_code ec;
	fs::remove(path, ec);

	WritePrivateProfileString("Texture", "LOD_Chr", "0", path.string().c_str());
	WritePrivateProfileString("Texture", "LOD_Shape", "1", path.string().c_str());
	WritePrivateProfileString("Shadow", "Use", "1", path.string().c_str());
	WritePrivateProfileString("Texture", "LOD_Chr", "1", path.string().c_str()); // overwrite

	EXPECT_EQ(GetPrivateProfileInt("Texture", "LOD_Chr", -1, path.string().c_str()), 1);
	EXPECT_EQ(GetPrivateProfileInt("Texture", "LOD_Shape", -1, path.string().c_str()), 1);
	EXPECT_EQ(GetPrivateProfileInt("Shadow", "Use", -1, path.string().c_str()), 1);

	fs::remove(path, ec);
}

#endif // !_WIN32
