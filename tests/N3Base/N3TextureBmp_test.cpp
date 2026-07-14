// BMP texture loading (docs/PORT_POSIX_PLAN.md, F9): the POSIX build has no
// D3DX equivalent for non-DXT formats. Besides TGA, the sky's sun disk / glow
// / flare ship as uncompressed Windows BMPs (8-bit palettised, BI_RGB), which
// D3DX loaded transparently on Windows. An undecoded BMP left the sun's texture
// stage unbound, and under the sun's additive (ONE/ONE) blend an unbound stage
// samples opaque white - painting a blown-out white quad instead of the sun.
// This pins CN3Texture::LoadFromFile against a synthetic 8-bit palettised BMP
// with known pixels (the exact format the sun textures use).
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
#include <vector>

namespace fs = std::filesystem;

namespace
{
fs::path MakeTempBmpPath()
{
	static std::atomic<uint32_t> s_counter = 0;
	static const time_t s_time             = time(nullptr);
	return fs::temp_directory_path()
		   / ("N3TextureBmpTest_" + std::to_string(s_time) + "_" + std::to_string(s_counter++) + ".bmp");
}

void Put32(std::vector<uint8_t>& v, uint32_t x)
{
	v.push_back(x & 0xff);
	v.push_back((x >> 8) & 0xff);
	v.push_back((x >> 16) & 0xff);
	v.push_back((x >> 24) & 0xff);
}

void Put16(std::vector<uint8_t>& v, uint16_t x)
{
	v.push_back(x & 0xff);
	v.push_back((x >> 8) & 0xff);
}
} // namespace

TEST(N3TextureBmpTest, LoadsSynthetic8BitPalettised)
{
	// 2x2 bottom-up (standard BMP) 8bpp image with a 2-entry palette:
	//   index 0 -> black (like the sun's background), index 1 -> white (glow).
	// Layout matches the sun BMPs: the disk/glow is white-on-black so the
	// additive blend at render time drops the black.
	const fs::path path = MakeTempBmpPath();
	{
		const uint32_t offBits = 14 + 40 + 2 * 4; // headers + 2 palette entries
		// Bottom-up rows, each padded to 4 bytes. 2px * 1 byte = 2, padded to 4.
		const uint8_t row0[4] = { 1, 0, 0, 0 }; // bottom row: white, black
		const uint8_t row1[4] = { 0, 1, 0, 0 }; // top row:    black, white
		const uint32_t fileSize = offBits + 8;

		std::vector<uint8_t> bytes;
		bytes.push_back('B');
		bytes.push_back('M');
		Put32(bytes, fileSize);
		Put32(bytes, 0);       // reserved
		Put32(bytes, offBits); // pixel data offset
		// BITMAPINFOHEADER
		Put32(bytes, 40);      // biSize
		Put32(bytes, 2);       // biWidth
		Put32(bytes, 2);       // biHeight (positive = bottom-up)
		Put16(bytes, 1);       // biPlanes
		Put16(bytes, 8);       // biBitCount
		Put32(bytes, 0);       // biCompression = BI_RGB
		Put32(bytes, 8);       // biSizeImage
		Put32(bytes, 0);       // biXPelsPerMeter
		Put32(bytes, 0);       // biYPelsPerMeter
		Put32(bytes, 2);       // biClrUsed
		Put32(bytes, 0);       // biClrImportant
		// Palette (B, G, R, reserved)
		bytes.insert(bytes.end(), { 0, 0, 0, 0 });         // 0 = black
		bytes.insert(bytes.end(), { 255, 255, 255, 0 });   // 1 = white
		bytes.insert(bytes.end(), row0, row0 + 4);
		bytes.insert(bytes.end(), row1, row1 + 4);

		FileWriter file;
		ASSERT_TRUE(file.Create(path));
		file.Write(bytes.data(), static_cast<int>(bytes.size()));
		file.Close();
	}

	RHIDeviceNull device;
	CN3Base::RHIDeviceSet(&device);
	CN3Base::PathSet("");

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

		// Output is top-down BGRA. Top row (y=0) = black, white.
		EXPECT_EQ(pixelAt(0, 0)[0], 0);
		EXPECT_EQ(pixelAt(0, 0)[2], 0);
		EXPECT_EQ(pixelAt(0, 0)[3], 255); // opaque
		EXPECT_EQ(pixelAt(1, 0)[0], 255);
		EXPECT_EQ(pixelAt(1, 0)[2], 255);

		// Bottom row (y=1) = white, black.
		EXPECT_EQ(pixelAt(0, 1)[0], 255);
		EXPECT_EQ(pixelAt(0, 1)[2], 255);
		EXPECT_EQ(pixelAt(1, 1)[0], 0);
		EXPECT_EQ(pixelAt(1, 1)[2], 0);

		EXPECT_EQ(pTex->UnlockRect(0), D3D_OK);
	}

	CN3Base::RHIDeviceSet(nullptr);

	std::error_code ec;
	fs::remove(path, ec);
}

#endif // !_WIN32
