#include <gtest/gtest.h>

#include <SDLGPUTranslate.h>

// Pure D3D9 -> SDL_GPU mapping helpers for the F6b backend; no GPU needed.

TEST(SdlGpuTranslateTest, FanExpansionProducesTriangleList)
{
	// A quad drawn as a 2-triangle fan (the engine's most common UI draw).
	const std::vector<uint16_t> indices = sgtr::ExpandFanIndices(2);
	const std::vector<uint16_t> expected = { 0, 1, 2, 0, 2, 3 };
	EXPECT_EQ(indices, expected);
}

TEST(SdlGpuTranslateTest, FanExpansionOfSingleTriangle)
{
	const std::vector<uint16_t> indices = sgtr::ExpandFanIndices(1);
	const std::vector<uint16_t> expected = { 0, 1, 2 };
	EXPECT_EQ(indices, expected);
}

TEST(SdlGpuTranslateTest, PrimitiveTopologyMapsFansToLists)
{
	EXPECT_EQ(sgtr::PrimitiveTopology(D3DPT_TRIANGLEFAN), SDL_GPU_PRIMITIVETYPE_TRIANGLELIST);
	EXPECT_EQ(sgtr::PrimitiveTopology(D3DPT_TRIANGLESTRIP), SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP);
	EXPECT_EQ(sgtr::PrimitiveTopology(D3DPT_LINELIST), SDL_GPU_PRIMITIVETYPE_LINELIST);
	EXPECT_EQ(sgtr::PrimitiveTopology(D3DPT_POINTLIST), SDL_GPU_PRIMITIVETYPE_POINTLIST);
}

TEST(SdlGpuTranslateTest, TextureFormatsMirrorTheGLBackend)
{
	EXPECT_EQ(sgtr::TranslateTexFormat(D3DFMT_DXT1).format, SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM);
	EXPECT_EQ(sgtr::TranslateTexFormat(D3DFMT_DXT3).format, SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM);
	EXPECT_EQ(sgtr::TranslateTexFormat(D3DFMT_DXT5).format, SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM);
	EXPECT_FALSE(sgtr::TranslateTexFormat(D3DFMT_DXT1).expandToBgra8);

	const sgtr::TexUploadFormat x8 = sgtr::TranslateTexFormat(D3DFMT_X8R8G8B8);
	EXPECT_EQ(x8.format, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);
	EXPECT_TRUE(x8.fillAlpha);

	EXPECT_TRUE(sgtr::TranslateTexFormat(D3DFMT_A4R4G4B4).expandToBgra8);
	EXPECT_TRUE(sgtr::TranslateTexFormat(D3DFMT_A1R5G5B5).expandToBgra8);
	EXPECT_TRUE(sgtr::TranslateTexFormat(D3DFMT_R8G8B8).expandToBgra8);
	EXPECT_FALSE(sgtr::TranslateTexFormat(D3DFMT_UNKNOWN).valid);
}

TEST(SdlGpuTranslateTest, ExpandsA4R4G4B4ToBgra8)
{
	// One pixel: A=0xF, R=0x8, G=0x4, B=0x0 -> D3D packs it as 0xF840.
	const uint8_t src[2] = { 0x40, 0xF8 }; // little-endian uint16
	const std::vector<uint8_t> out = sgtr::ExpandToBgra8(D3DFMT_A4R4G4B4, src, 1, 1, 2);
	ASSERT_EQ(out.size(), 4u);
	EXPECT_EQ(out[0], 0x00);      // B = 0x0 * 17
	EXPECT_EQ(out[1], 4 * 17);    // G
	EXPECT_EQ(out[2], 8 * 17);    // R
	EXPECT_EQ(out[3], 15 * 17);   // A
}

TEST(SdlGpuTranslateTest, ExpandsA1R5G5B5ToBgra8)
{
	// A=1, R=31, G=0, B=31 -> 0xFC1F.
	const uint8_t src[2] = { 0x1F, 0xFC };
	const std::vector<uint8_t> out = sgtr::ExpandToBgra8(D3DFMT_A1R5G5B5, src, 1, 1, 2);
	ASSERT_EQ(out.size(), 4u);
	EXPECT_EQ(out[0], 0xFF); // B
	EXPECT_EQ(out[1], 0x00); // G
	EXPECT_EQ(out[2], 0xFF); // R
	EXPECT_EQ(out[3], 0xFF); // A
}

TEST(SdlGpuTranslateTest, SamplerKeyRoundTripsEveryField)
{
	const uint32_t key = sgtr::SamplerKey(
		D3DTEXF_LINEAR, D3DTEXF_POINT, D3DTEXF_LINEAR, D3DTADDRESS_CLAMP, D3DTADDRESS_MIRROR, true);
	EXPECT_EQ(key & 0xF, DWORD(D3DTEXF_LINEAR));
	EXPECT_EQ((key >> 4) & 0xF, DWORD(D3DTEXF_POINT));
	EXPECT_EQ((key >> 8) & 0xF, DWORD(D3DTEXF_LINEAR));
	EXPECT_EQ((key >> 12) & 0xF, DWORD(D3DTADDRESS_CLAMP));
	EXPECT_EQ((key >> 16) & 0xF, DWORD(D3DTADDRESS_MIRROR));
	EXPECT_NE(key & (1u << 20), 0u);

	// Different mips flag -> different key (different SDL sampler).
	EXPECT_NE(key,
		sgtr::SamplerKey(D3DTEXF_LINEAR, D3DTEXF_POINT, D3DTEXF_LINEAR, D3DTADDRESS_CLAMP,
			D3DTADDRESS_MIRROR, false));
}

TEST(SdlGpuTranslateTest, MipModePinsBaseLevelWithoutMips)
{
	EXPECT_EQ(sgtr::MipMode(D3DTEXF_LINEAR, false).maxLod, 0.0f);
	EXPECT_EQ(sgtr::MipMode(D3DTEXF_NONE, true).maxLod, 0.0f);
	EXPECT_GT(sgtr::MipMode(D3DTEXF_LINEAR, true).maxLod, 100.0f);
	EXPECT_EQ(sgtr::MipMode(D3DTEXF_LINEAR, true).mode, SDL_GPU_SAMPLERMIPMAPMODE_LINEAR);
	EXPECT_EQ(sgtr::MipMode(D3DTEXF_POINT, true).mode, SDL_GPU_SAMPLERMIPMAPMODE_NEAREST);
}
