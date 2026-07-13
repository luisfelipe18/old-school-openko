// TGA texture loading (docs/PORT_POSIX_PLAN.md, F9): the POSIX build has no
// D3DX equivalent for non-DXT formats, so CN3Texture::LoadFromFile decodes
// TGA directly (docs/PORT_POSIX_PLAN.md notes this was narrowed from "every
// non-DXT format" to TGA specifically, since that's the only one the shipped
// assets - sky/moon phases, some UI art - actually use). This pins the
// decoder against a synthetic uncompressed 24bpp TGA with known pixels.
//
// POSIX-only: on Windows this path goes through D3DXCreateTextureFromFileEx
// (no D3D9 device is available in this headless test binary either way).

#ifndef _WIN32

#include <gtest/gtest.h>

#include <FileIO/FileWriter.h>
#include <N3Base/N3Base.h>
#include <N3Base/N3Texture.h>
#include <N3Base/RHI/RHIDeviceNull.h>

#include <atomic>
#include <cstdint>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
fs::path MakeTempTgaPath()
{
	static std::atomic<uint32_t> s_counter = 0;
	static const time_t s_time             = time(nullptr);
	return fs::temp_directory_path()
		   / ("N3TextureTgaTest_" + std::to_string(s_time) + "_" + std::to_string(s_counter++) + ".tga");
}

#pragma pack(push, 1)
struct TgaHeader
{
	uint8_t idLength      = 0;
	uint8_t colorMapType  = 0;
	uint8_t imageType     = 2; // uncompressed true-color
	uint8_t colorMapSpec[5] = {};
	uint16_t xOrigin      = 0;
	uint16_t yOrigin      = 0;
	uint16_t width        = 0;
	uint16_t height       = 0;
	uint8_t bitsPerPixel  = 24;
	uint8_t imageDescriptor = 0x20; // top-left origin, no interleave
};
#pragma pack(pop)
static_assert(sizeof(TgaHeader) == 18, "TGA header must be exactly 18 bytes on the wire");
} // namespace

TEST(N3TextureTgaTest, LoadsSyntheticUncompressed24Bit)
{
	// 2x2 top-origin BGR image: red, green / blue, yellow.
	const fs::path path = MakeTempTgaPath();
	{
		FileWriter file;
		ASSERT_TRUE(file.Create(path));

		TgaHeader hdr;
		hdr.width  = 2;
		hdr.height = 2;
		file.Write(&hdr, sizeof(hdr));

		const uint8_t pixels[2 * 2 * 3] = {
			0, 0, 255,   0, 255, 0,   // row 0 (top): red, green
			255, 0, 0,   0, 255, 255, // row 1 (bottom): blue, yellow
		};
		file.Write(pixels, sizeof(pixels));
		file.Close();
	}

	RHIDeviceNull device;
	CN3Base::RHIDeviceSet(&device);
	CN3Base::PathSet(""); // ensure LoadFromFile takes the path verbatim below

	{
		CN3Texture tex;
		ASSERT_TRUE(tex.LoadFromFile(path.string()));
		EXPECT_EQ(tex.Width(), 2u);
		EXPECT_EQ(tex.Height(), 2u);
		EXPECT_EQ(tex.PixelFormat(), D3DFMT_A8R8G8B8);

		IRHITexture* pTex = tex.Get();
		ASSERT_NE(pTex, nullptr);
		D3DLOCKED_RECT lr = {};
		ASSERT_EQ(pTex->LockRect(0, &lr, nullptr, 0), D3D_OK);

		auto pixelAt = [&](int x, int y) -> const uint8_t* {
			return static_cast<const uint8_t*>(lr.pBits) + static_cast<size_t>(y) * lr.Pitch
				   + static_cast<size_t>(x) * 4;
		};

		// BGRA in memory, matching D3DFMT_A8R8G8B8.
		EXPECT_EQ(pixelAt(0, 0)[0], 0);
		EXPECT_EQ(pixelAt(0, 0)[1], 0);
		EXPECT_EQ(pixelAt(0, 0)[2], 255);
		EXPECT_EQ(pixelAt(0, 0)[3], 255);

		EXPECT_EQ(pixelAt(1, 0)[0], 0);
		EXPECT_EQ(pixelAt(1, 0)[1], 255);
		EXPECT_EQ(pixelAt(1, 0)[2], 0);

		EXPECT_EQ(pixelAt(0, 1)[0], 255);
		EXPECT_EQ(pixelAt(0, 1)[1], 0);
		EXPECT_EQ(pixelAt(0, 1)[2], 0);

		EXPECT_EQ(pixelAt(1, 1)[0], 0);
		EXPECT_EQ(pixelAt(1, 1)[1], 255);
		EXPECT_EQ(pixelAt(1, 1)[2], 255);

		EXPECT_EQ(pTex->UnlockRect(0), D3D_OK);
	}

	CN3Base::RHIDeviceSet(nullptr);

	std::error_code ec;
	fs::remove(path, ec);
}

TEST(N3TextureTgaTest, RejectsUnsupportedImageType)
{
	// Palettised (colorMapType 1 / imageType 1) TGAs aren't handled - nothing
	// in the shipped asset set uses them - so this must fail cleanly rather
	// than misinterpret the pixel data.
	const fs::path path = MakeTempTgaPath();
	{
		FileWriter file;
		ASSERT_TRUE(file.Create(path));

		TgaHeader hdr;
		hdr.colorMapType = 1;
		hdr.imageType    = 1;
		hdr.width        = 1;
		hdr.height       = 1;
		file.Write(&hdr, sizeof(hdr));
		file.Close();
	}

	RHIDeviceNull device;
	CN3Base::RHIDeviceSet(&device);
	CN3Base::PathSet("");

	{
		CN3Texture tex;
		EXPECT_FALSE(tex.LoadFromFile(path.string()));
	}

	CN3Base::RHIDeviceSet(nullptr);

	std::error_code ec;
	fs::remove(path, ec);
}

#endif // !_WIN32
