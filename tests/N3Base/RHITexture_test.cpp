// Headless texture tests (docs/PORT_POSIX_PLAN.md, task T6.2 acceptance):
//   1. RHITextureNull allocates per-level storage with the right sizes,
//      including the 4x4 block layout for DXT formats.
//   2. A synthetic .dxt in the game's real NTF layout loads through
//      CN3Texture::Load -> RHIDevice()->CreateTexture -> LockRect, and its
//      compressed payload round-trips into the Null texture headlessly.

#include <gtest/gtest.h>

#include <FileIO/FileReader.h>
#include <FileIO/FileWriter.h>
#include <N3Base/N3Base.h>
#include <N3Base/N3Texture.h>
#include <N3Base/RHI/RHIDeviceNull.h>

#include <atomic>
#include <cstring>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
fs::path MakeTempPath()
{
	static std::atomic<uint32_t> s_counter = 0;
	static const time_t s_time             = time(nullptr);
	return fs::temp_directory_path()
		   / ("RHITexTest_" + std::to_string(s_time) + "_" + std::to_string(s_counter++) + ".dxt");
}
} // namespace

TEST(RHITextureNullTest, DXT1LevelSizesUse4x4Blocks)
{
	RHIDeviceNull device;

	// 8x8 DXT1 with a full mip chain down to 4x4: levels 8x8, 4x4.
	IRHITexture* pTex = nullptr;
	ASSERT_EQ(device.CreateTexture(8, 8, 0, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &pTex), D3D_OK);
	ASSERT_NE(pTex, nullptr);

	// 8x8 -> 4x4 -> 2x2 -> 1x1 (D3D9 full chain), 4 levels.
	EXPECT_EQ(pTex->GetLevelCount(), 4u);

	D3DSURFACE_DESC sd = {};
	ASSERT_EQ(pTex->GetLevelDesc(0, &sd), D3D_OK);
	EXPECT_EQ(sd.Width, 8u);
	EXPECT_EQ(sd.Height, 8u);
	EXPECT_EQ(sd.Format, D3DFMT_DXT1);

	// Level 0: 2x2 blocks * 8 bytes = 16 bytes/row-of-blocks pitch is 2*8=16.
	D3DLOCKED_RECT lr = {};
	ASSERT_EQ(pTex->LockRect(0, &lr, nullptr, 0), D3D_OK);
	EXPECT_EQ(lr.Pitch, 16); // 2 blocks wide * 8 bytes (DXT1)
	EXPECT_NE(lr.pBits, nullptr);
	EXPECT_EQ(pTex->UnlockRect(0), D3D_OK);

	EXPECT_NE(pTex->LockRect(99, &lr, nullptr, 0), D3D_OK); // out-of-range level

	EXPECT_EQ(pTex->Release(), 0u);
}

TEST(RHITextureNullTest, UncompressedLevelSizesUseBytesPerPixel)
{
	RHIDeviceNull device;

	IRHITexture* pTex = nullptr;
	ASSERT_EQ(device.CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pTex), D3D_OK);
	EXPECT_EQ(pTex->GetLevelCount(), 1u);

	D3DLOCKED_RECT lr = {};
	ASSERT_EQ(pTex->LockRect(0, &lr, nullptr, 0), D3D_OK);
	EXPECT_EQ(lr.Pitch, 16); // 4 px * 4 bytes
	EXPECT_EQ(pTex->UnlockRect(0), D3D_OK);

	EXPECT_EQ(pTex->Release(), 0u);
}

TEST(N3TextureHeadlessTest, LoadsSyntheticDXT1)
{
	// Build an 8x8 DXT1 texture, no mip map, "NTF" version 3 (no encryption).
	uint8_t dxtData[32];
	for (int i = 0; i < 32; ++i)
		dxtData[i] = static_cast<uint8_t>(0x40 + i);

	const fs::path path = MakeTempPath();
	{
		FileWriter file;
		ASSERT_TRUE(file.Create(path));

		const int nameLength = 0; // CN3BaseFileAccess::Load name field
		file.Write(&nameLength, 4);

		CN3Texture::__DxtHeader hdr = {};
		hdr.szID[0]                 = 'N';
		hdr.szID[1]                 = 'T';
		hdr.szID[2]                 = 'F';
		hdr.szID[3]                 = 3;
		hdr.nWidth                  = 8;
		hdr.nHeight                 = 8;
		hdr.Format                  = D3DFMT_DXT1;
		hdr.bMipMap                 = FALSE;
		file.Write(&hdr, sizeof(hdr));

		file.Write(dxtData, sizeof(dxtData)); // compressed level-0 payload

		// The loader seeks past a half-size uncompressed copy (nW*nH/4 bytes)
		// kept for cards without DXT support; provide it so the seek stays valid.
		uint8_t extra[16] = {};
		file.Write(extra, sizeof(extra));

		file.Close();
	}

	// Advertise DXT1 support and a generous max texture size so Create() keeps
	// the requested dimensions and takes the compressed path.
	const uint32_t savedCaps      = CN3Base::s_dwTextureCaps;
	const D3DCAPS9 savedDevCaps   = CN3Base::s_DevCaps;
	CN3Base::s_dwTextureCaps      = TEX_CAPS_DXT1;
	CN3Base::s_DevCaps.MaxTextureWidth  = 4096;
	CN3Base::s_DevCaps.MaxTextureHeight = 4096;

	RHIDeviceNull device;
	CN3Base::RHIDeviceSet(&device);

	{
		CN3Texture tex;
		FileReader file;
		ASSERT_TRUE(file.OpenExisting(path));
		ASSERT_TRUE(tex.Load(file));

		EXPECT_EQ(tex.Width(), 8u);
		EXPECT_EQ(tex.Height(), 8u);
		EXPECT_EQ(tex.PixelFormat(), D3DFMT_DXT1);
		EXPECT_EQ(tex.MipMapCount(), 1);

		// The compressed payload must have landed in the Null texture verbatim.
		IRHITexture* pTex = tex.Get();
		ASSERT_NE(pTex, nullptr);
		D3DLOCKED_RECT lr = {};
		ASSERT_EQ(pTex->LockRect(0, &lr, nullptr, 0), D3D_OK);
		EXPECT_EQ(std::memcmp(lr.pBits, dxtData, sizeof(dxtData)), 0);
		EXPECT_EQ(pTex->UnlockRect(0), D3D_OK);
	}

	CN3Base::RHIDeviceSet(nullptr);
	CN3Base::s_dwTextureCaps = savedCaps;
	CN3Base::s_DevCaps       = savedDevCaps;

	std::error_code ec;
	fs::remove(path, ec);
}
