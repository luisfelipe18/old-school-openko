// D3D9 -> OpenGL translation helpers for the RHI GL backend (T6.6/T6.7).
// Pure functions, so the FVF vertex layouts and enum mappings are verified
// headlessly against the engine's real vertex structs.

#include <gtest/gtest.h>

#include <GLTranslate.h>

#include <N3Base/My_3DStruct.h>

TEST(GLTranslateTest, FVFLayoutsMatchEngineVertexStructs)
{
	// FVF_VNT1: position + normal + 1 UV set (__VertexT1).
	const gltr::FVFLayout vnt1 = gltr::ParseFVF(FVF_VNT1);
	ASSERT_TRUE(vnt1.valid);
	EXPECT_FALSE(vnt1.xyzrhw);
	EXPECT_EQ(vnt1.stride, static_cast<int>(sizeof(__VertexT1)));
	EXPECT_EQ(vnt1.posOffset, 0);
	EXPECT_EQ(vnt1.normalOffset, 12);
	EXPECT_EQ(vnt1.colorOffset, -1);
	EXPECT_EQ(vnt1.uvCount, 1);
	EXPECT_EQ(vnt1.uvOffset[0], 24);

	// FVF_VNT2 adds a second UV set (__VertexT2).
	const gltr::FVFLayout vnt2 = gltr::ParseFVF(FVF_VNT2);
	ASSERT_TRUE(vnt2.valid);
	EXPECT_EQ(vnt2.stride, static_cast<int>(sizeof(__VertexT2)));
	EXPECT_EQ(vnt2.uvCount, 2);
	EXPECT_EQ(vnt2.uvOffset[1], 32);

	// FVF_XYZCOLOR / FVF_CV: position + D3DCOLOR (__VertexXyzColor).
	const gltr::FVFLayout cv = gltr::ParseFVF(FVF_XYZCOLOR);
	ASSERT_TRUE(cv.valid);
	EXPECT_EQ(cv.stride, static_cast<int>(sizeof(__VertexXyzColor)));
	EXPECT_EQ(cv.colorOffset, 12);
	EXPECT_EQ(cv.uvCount, 0);

	// FVF_XYZT1: position + 1 UV set (__VertexXyzT1).
	const gltr::FVFLayout xyzt1 = gltr::ParseFVF(FVF_XYZT1);
	ASSERT_TRUE(xyzt1.valid);
	EXPECT_EQ(xyzt1.stride, static_cast<int>(sizeof(__VertexXyzT1)));
	EXPECT_EQ(xyzt1.uvOffset[0], 12);

	// FVF_TRANSFORMED: XYZRHW + color + 1 UV (__VertexTransformed).
	const gltr::FVFLayout xformed = gltr::ParseFVF(FVF_TRANSFORMED);
	ASSERT_TRUE(xformed.valid);
	EXPECT_TRUE(xformed.xyzrhw);
	EXPECT_EQ(xformed.stride, static_cast<int>(sizeof(__VertexTransformed)));
	EXPECT_EQ(xformed.colorOffset, 16);
	EXPECT_EQ(xformed.uvOffset[0], 20);

	// FVF_TRANSFORMEDCOLOR: XYZRHW + color (__VertexTransformedColor).
	const gltr::FVFLayout xcolor = gltr::ParseFVF(FVF_TRANSFORMEDCOLOR);
	ASSERT_TRUE(xcolor.valid);
	EXPECT_EQ(xcolor.stride, static_cast<int>(sizeof(__VertexTransformedColor)));
	EXPECT_EQ(xcolor.colorOffset, 16);
	EXPECT_EQ(xcolor.uvCount, 0);
}

TEST(GLTranslateTest, EnumMappings)
{
	EXPECT_EQ(gltr::BlendFactor(D3DBLEND_SRCALPHA), gl::SRC_ALPHA);
	EXPECT_EQ(gltr::BlendFactor(D3DBLEND_INVSRCALPHA), gl::ONE_MINUS_SRC_ALPHA);
	EXPECT_EQ(gltr::BlendFactor(D3DBLEND_ZERO), gl::ZERO);
	EXPECT_EQ(gltr::BlendFactor(D3DBLEND_ONE), gl::ONE);

	EXPECT_EQ(gltr::CompareFunc(D3DCMP_LESSEQUAL), gl::LEQUAL);
	EXPECT_EQ(gltr::CompareFunc(D3DCMP_GREATEREQUAL), gl::GEQUAL);

	EXPECT_EQ(gltr::PrimitiveMode(D3DPT_TRIANGLELIST), gl::TRIANGLES);
	EXPECT_EQ(gltr::PrimitiveMode(D3DPT_TRIANGLEFAN), gl::TRIANGLE_FAN);
	EXPECT_EQ(gltr::PrimitiveMode(D3DPT_LINELIST), gl::LINES);

	EXPECT_EQ(gltr::PrimitiveElementCount(D3DPT_TRIANGLELIST, 2), 6u);
	EXPECT_EQ(gltr::PrimitiveElementCount(D3DPT_TRIANGLEFAN, 2), 4u);
	EXPECT_EQ(gltr::PrimitiveElementCount(D3DPT_TRIANGLESTRIP, 2), 4u);
	EXPECT_EQ(gltr::PrimitiveElementCount(D3DPT_LINELIST, 3), 6u);

	EXPECT_EQ(gltr::WrapMode(D3DTADDRESS_WRAP), gl::REPEAT);
	EXPECT_EQ(gltr::WrapMode(D3DTADDRESS_BORDER), gl::CLAMP_TO_BORDER);

	// Filters: mip NONE means no mipmapping regardless of the mip texture.
	EXPECT_EQ(gltr::MinFilter(D3DTEXF_LINEAR, D3DTEXF_NONE, true), gl::LINEAR);
	EXPECT_EQ(gltr::MinFilter(D3DTEXF_LINEAR, D3DTEXF_LINEAR, true), gl::LINEAR_MIPMAP_LINEAR);
	EXPECT_EQ(gltr::MinFilter(D3DTEXF_LINEAR, D3DTEXF_POINT, true), gl::LINEAR_MIPMAP_NEAREST);
	EXPECT_EQ(gltr::MinFilter(D3DTEXF_LINEAR, D3DTEXF_LINEAR, false), gl::LINEAR);
	EXPECT_EQ(gltr::MinFilter(D3DTEXF_POINT, D3DTEXF_NONE, false), gl::NEAREST);
}

TEST(GLTranslateTest, TextureFormats)
{
	const gltr::TexUploadFormat dxt1 = gltr::TranslateTexFormat(D3DFMT_DXT1);
	ASSERT_TRUE(dxt1.valid);
	EXPECT_TRUE(dxt1.compressed);
	EXPECT_EQ(dxt1.internalFormat, gl::COMPRESSED_RGBA_S3TC_DXT1);

	const gltr::TexUploadFormat dxt3 = gltr::TranslateTexFormat(D3DFMT_DXT3);
	EXPECT_EQ(dxt3.internalFormat, gl::COMPRESSED_RGBA_S3TC_DXT3);
	const gltr::TexUploadFormat dxt5 = gltr::TranslateTexFormat(D3DFMT_DXT5);
	EXPECT_EQ(dxt5.internalFormat, gl::COMPRESSED_RGBA_S3TC_DXT5);

	const gltr::TexUploadFormat argb = gltr::TranslateTexFormat(D3DFMT_A8R8G8B8);
	ASSERT_TRUE(argb.valid);
	EXPECT_FALSE(argb.compressed);
	EXPECT_EQ(argb.internalFormat, gl::RGBA8);
	EXPECT_EQ(argb.format, gl::BGRA); // D3DCOLOR memory order

	const gltr::TexUploadFormat a1555 = gltr::TranslateTexFormat(D3DFMT_A1R5G5B5);
	EXPECT_EQ(a1555.type, gl::UNSIGNED_SHORT_1_5_5_5_REV);

	EXPECT_FALSE(gltr::TranslateTexFormat(D3DFMT_UNKNOWN).valid);
}
