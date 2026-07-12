#include <gtest/gtest.h>

#include <KscViewerCore.h>

#include <jpeglib.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace
{
// Encrypts plaintext with the game's stream cipher - the inverse of
// ksc_core::DecryptKsc's decoder, kept here so the test builds .ksc fixtures
// without depending on the (Windows-only) CJpegFile::Encrypt path.
std::vector<uint8_t> EncryptKsc(const std::vector<uint8_t>& jpeg)
{
	uint16_t r                   = 1124;
	constexpr uint16_t c1        = 52845;
	constexpr uint16_t c2        = 22719;
	const auto encrypt = [&](uint8_t plain) -> uint8_t
	{
		const uint8_t cipher = static_cast<uint8_t>(plain ^ (r >> 8));
		r                    = static_cast<uint16_t>((cipher + r) * c1 + c2);
		return cipher;
	};

	// 4 arbitrary header bytes, then the "KSC\x01" magic, then the payload.
	const uint8_t header[8] = { 0x11, 0x22, 0x33, 0x44, 'K', 'S', 'C', 0x01 };
	std::vector<uint8_t> out;
	out.reserve(8 + jpeg.size());
	for (uint8_t b : header)
		out.push_back(encrypt(b));
	for (uint8_t b : jpeg)
		out.push_back(encrypt(b));
	return out;
}

// Writes a solid-color WxH RGB JPEG to disk via libjpeg.
bool WriteTestJpeg(const fs::path& path, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
	FILE* out = fopen(path.string().c_str(), "wb");
	if (out == nullptr)
		return false;

	jpeg_compress_struct cinfo;
	jpeg_error_mgr jerr;
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, out);

	cinfo.image_width      = static_cast<JDIMENSION>(w);
	cinfo.image_height     = static_cast<JDIMENSION>(h);
	cinfo.input_components = 3;
	cinfo.in_color_space   = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, 90, TRUE);
	jpeg_start_compress(&cinfo, TRUE);

	std::vector<uint8_t> row(static_cast<size_t>(w) * 3);
	for (int x = 0; x < w; ++x)
	{
		row[x * 3 + 0] = r;
		row[x * 3 + 1] = g;
		row[x * 3 + 2] = b;
	}
	while (cinfo.next_scanline < cinfo.image_height)
	{
		JSAMPROW rowPtr = row.data();
		jpeg_write_scanlines(&cinfo, &rowPtr, 1);
	}
	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	fclose(out);
	return true;
}

std::vector<uint8_t> ReadAll(const fs::path& path)
{
	std::ifstream f(path, std::ios::binary);
	return std::vector<uint8_t>(
		(std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void WriteAll(const fs::path& path, const std::vector<uint8_t>& bytes)
{
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	f.write(reinterpret_cast<const char*>(bytes.data()),
		static_cast<std::streamsize>(bytes.size()));
}

class KscViewerCoreTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		dir = fs::temp_directory_path() / "openko-kscviewer-test";
		std::error_code ec;
		fs::remove_all(dir, ec);
		fs::create_directories(dir, ec);
	}
	void TearDown() override
	{
		std::error_code ec;
		fs::remove_all(dir, ec);
	}
	fs::path dir;
};
} // namespace

TEST_F(KscViewerCoreTest, DecryptRoundTripsThroughTheCipher)
{
	const std::vector<uint8_t> payload = { 0xFF, 0xD8, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x42 };
	const std::vector<uint8_t> ksc     = EncryptKsc(payload);
	EXPECT_EQ(ksc_core::DecryptKsc(ksc), payload);
}

TEST_F(KscViewerCoreTest, DecryptRejectsShortAndBadMagic)
{
	EXPECT_TRUE(ksc_core::DecryptKsc({}).empty());
	EXPECT_TRUE(ksc_core::DecryptKsc({ 1, 2, 3, 4, 5, 6, 7 }).empty()); // < 8 bytes

	// 8 bytes of zeros won't decrypt to the KSC magic.
	EXPECT_TRUE(ksc_core::DecryptKsc(std::vector<uint8_t>(16, 0)).empty());
}

TEST_F(KscViewerCoreTest, IsKscPathIsCaseInsensitive)
{
	EXPECT_TRUE(ksc_core::IsKscPath("splash.ksc"));
	EXPECT_TRUE(ksc_core::IsKscPath("SPLASH.KSC"));
	EXPECT_FALSE(ksc_core::IsKscPath("splash.jpg"));
	EXPECT_FALSE(ksc_core::IsKscPath("splash"));
}

TEST_F(KscViewerCoreTest, LoadsPlainJpg)
{
	const fs::path jpg = dir / "plain.jpg";
	ASSERT_TRUE(WriteTestJpeg(jpg, 32, 24, 200, 40, 40));

	const ksc_core::DecodedImage image = ksc_core::LoadImage(jpg);
	ASSERT_TRUE(image.IsValid());
	EXPECT_EQ(image.width, 32);
	EXPECT_EQ(image.height, 24);
	// Center pixel should be roughly the red we wrote (JPEG is lossy).
	const size_t center = (static_cast<size_t>(12) * 32 + 16) * 3;
	EXPECT_GT(image.rgb[center + 0], 150);
	EXPECT_LT(image.rgb[center + 1], 90);
	EXPECT_LT(image.rgb[center + 2], 90);
}

TEST_F(KscViewerCoreTest, LoadsEncryptedKsc)
{
	// Real JPEG bytes, encrypted into a .ksc, decoded back through the tool.
	const fs::path jpg = dir / "src.jpg";
	ASSERT_TRUE(WriteTestJpeg(jpg, 48, 16, 30, 200, 60));
	const std::vector<uint8_t> ksc = EncryptKsc(ReadAll(jpg));

	const fs::path kscPath = dir / "image.ksc";
	WriteAll(kscPath, ksc);

	const ksc_core::DecodedImage image = ksc_core::LoadImage(kscPath);
	ASSERT_TRUE(image.IsValid());
	EXPECT_EQ(image.width, 48);
	EXPECT_EQ(image.height, 16);
	const size_t center = (static_cast<size_t>(8) * 48 + 24) * 3;
	EXPECT_LT(image.rgb[center + 0], 100);
	EXPECT_GT(image.rgb[center + 1], 150);
}

TEST_F(KscViewerCoreTest, ExportsKscToDecodableJpg)
{
	const fs::path jpg = dir / "orig.jpg";
	ASSERT_TRUE(WriteTestJpeg(jpg, 16, 16, 10, 20, 240));
	const std::vector<uint8_t> origJpeg = ReadAll(jpg);

	const fs::path kscPath = dir / "orig.ksc";
	WriteAll(kscPath, EncryptKsc(origJpeg));

	const fs::path outJpg = dir / "exported.jpg";
	ASSERT_TRUE(ksc_core::ExportJpg(kscPath, outJpg));

	// The exported bytes are exactly the original JPEG, and re-decode.
	EXPECT_EQ(ReadAll(outJpg), origJpeg);
	const ksc_core::DecodedImage image = ksc_core::LoadImage(outJpg);
	ASSERT_TRUE(image.IsValid());
	EXPECT_EQ(image.width, 16);
	EXPECT_EQ(image.height, 16);
}
