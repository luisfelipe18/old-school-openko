// Text-resource path on POSIX (docs/PORT_POSIX_PLAN.md, T6.4 acceptance):
// a TU using IDS_* compiles and runs the full fmt::format_text_resource
// lookup, which reads the Texts table (Data\Texts_*.tbl) that CGameBase now
// defines in ClientResourceFormatter.cpp so it links without the rest of the
// game. A synthetic, encrypted .tbl is written, loaded through the engine's
// own CN3TableBase loader, and formatted through the IDS_* enum.

#include <gtest/gtest.h>

#include <ClientResourceFormatter.h>
#include <GameBase.h>
#include <text_resources.h>

#include <FileIO/FileWriter.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace
{
fs::path MakeTempPath()
{
	static std::atomic<uint32_t> s_counter = 0;
	static const time_t s_time             = time(nullptr);
	return fs::temp_directory_path()
		   / ("TextResTest_" + std::to_string(s_time) + "_" + std::to_string(s_counter++) + ".tbl");
}

// Column data-type tokens, matching N3TableBaseImpl's TBL_DATA_TYPE.
constexpr uint32_t kDtDword  = 6;
constexpr uint32_t kDtString = 7;

void PutU32(std::vector<uint8_t>& out, uint32_t v)
{
	for (int i = 0; i < 4; ++i)
		out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
}

void PutString(std::vector<uint8_t>& out, const std::string& s)
{
	PutU32(out, static_cast<uint32_t>(s.size()));
	out.insert(out.end(), s.begin(), s.end());
}

// Reproduces the table tool's stream cipher (see CN3TableBaseImpl::LoadFromFile):
//   cipher = plain ^ (key_r >> 8); key_r = (cipher + key_r) * key_c1 + key_c2;
std::vector<uint8_t> Encrypt(const std::vector<uint8_t>& plain)
{
	uint16_t key_r        = 0x0816;
	const uint16_t key_c1 = 0x6081;
	const uint16_t key_c2 = 0x1608;

	std::vector<uint8_t> cipher;
	cipher.reserve(plain.size());
	for (uint8_t p : plain)
	{
		const uint8_t c = static_cast<uint8_t>(p ^ (key_r >> 8));
		key_r           = static_cast<uint16_t>((c + key_r) * key_c1 + key_c2);
		cipher.push_back(c);
	}
	return cipher;
}

// Writes an encrypted Texts .tbl (two columns: DWORD id, STRING text).
void WriteSyntheticTextsTable(
	const fs::path& path, const std::vector<std::pair<uint32_t, std::string>>& rows)
{
	std::vector<uint8_t> plain;

	PutU32(plain, 2);          // column count
	PutU32(plain, kDtDword);   // column 0: id
	PutU32(plain, kDtString);  // column 1: text
	PutU32(plain, static_cast<uint32_t>(rows.size()));
	for (const auto& [id, text] : rows)
	{
		PutU32(plain, id);
		PutString(plain, text);
	}

	const std::vector<uint8_t> cipher = Encrypt(plain);

	FileWriter file;
	ASSERT_TRUE(file.Create(path));
	file.Write(cipher.data(), cipher.size());
	file.Close();
}
} // namespace

TEST(TextResourceTest, MissingResourceFormatsToEmptyString)
{
	CGameBase::s_pTbl_Texts.Release();

	// Nothing loaded: the lookup fails cleanly and yields an empty string
	// rather than crashing (exercises the IDS_* path end to end on POSIX).
	EXPECT_EQ(fmt::format_text_resource(IDS_CLASS_KINDOF_WARRIOR), "");
}

TEST(TextResourceTest, LoadsAndFormatsFromSyntheticTable)
{
	const fs::path path = MakeTempPath();
	WriteSyntheticTextsTable(path,
		{
			{IDS_CLASS_KINDOF_WARRIOR, "Warrior"},
			{IDS_MSG_FMT_EXP_GET, "Earned %d Experience Points"},
			{IDS_FMT_CONNECT_ERROR, "Failed logging into the %s server. (%d)"},
		});

	ASSERT_TRUE(CGameBase::s_pTbl_Texts.LoadFromFile(path.string()));

	// Plain string.
	EXPECT_EQ(fmt::format_text_resource(IDS_CLASS_KINDOF_WARRIOR), "Warrior");

	// printf-style substitution through fmt::sprintf.
	EXPECT_EQ(fmt::format_text_resource(IDS_MSG_FMT_EXP_GET, 150), "Earned 150 Experience Points");
	EXPECT_EQ(fmt::format_text_resource(IDS_FMT_CONNECT_ERROR, "Karus", 42),
		"Failed logging into the Karus server. (42)");

	// IDs absent from the table still degrade to empty.
	EXPECT_EQ(fmt::format_text_resource(IDS_CLASS_KINDOF_ROGUE), "");

	CGameBase::s_pTbl_Texts.Release();

	std::error_code ec;
	fs::remove(path, ec);
}
