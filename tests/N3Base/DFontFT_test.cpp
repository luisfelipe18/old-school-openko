// POSIX DFont FreeType backend (docs/PORT_POSIX_PLAN.md, T7.1): SetText must
// rasterize real glyph coverage into the RHI texture and produce draw-ready
// quads, while degrading to a safe no-op when no font file is available.

#include <gtest/gtest.h>

#include <N3Base/DFont.h>
#include <N3Base/RHI/RHIDeviceNull.h>
#include <N3Base/RHI/RHITextures.h>

namespace
{
// Counts texels with a non-zero alpha nibble in the level-0 A4R4G4B4 surface.
int CountLitTexels(IRHITexture* pTexture)
{
	D3DSURFACE_DESC desc {};
	if (pTexture->GetLevelDesc(0, &desc) != D3D_OK)
		return -1;

	D3DLOCKED_RECT lr {};
	if (pTexture->LockRect(0, &lr, nullptr, 0) != D3D_OK)
		return -1;

	int iLit = 0;
	for (UINT y = 0; y < desc.Height; ++y)
	{
		const auto* pRow = reinterpret_cast<const uint16_t*>(
			static_cast<const uint8_t*>(lr.pBits) + static_cast<size_t>(y) * lr.Pitch);
		for (UINT x = 0; x < desc.Width; ++x)
		{
			if (pRow[x] & 0xF000)
				++iLit;
		}
	}

	pTexture->UnlockRect(0);
	return iLit;
}
} // namespace

TEST(DFontFTTest, LifecycleIsSafeWithoutText)
{
	RHIDeviceNull device;
	CN3Base::RHIDeviceSet(&device);

	{
		CDFont font("Arial", 16, 0);
		EXPECT_EQ(font.GetFontName(), "Arial");
		EXPECT_EQ(font.GetFontHeight(), 16u);

		EXPECT_EQ(font.InitDeviceObjects(nullptr), S_OK);
		EXPECT_EQ(font.RestoreDeviceObjects(), S_OK);

		// Nothing set: drawing is a safe no-op.
		EXPECT_FALSE(font.IsSetText());
		EXPECT_EQ(font.DrawText(10.0f, 10.0f, 0xFFFFFFFF, 0), S_OK);

		EXPECT_EQ(font.InvalidateDeviceObjects(), S_OK);
		EXPECT_EQ(font.DeleteDeviceObjects(), S_OK);
	}

	CN3Base::RHIDeviceSet(nullptr);
}

TEST(DFontFTTest, RasterizesTextIntoAtlas)
{
	RHIDeviceNull device;
	CN3Base::RHIDeviceSet(&device);

	{
		CDFont font("굴림", 16, 0); // the family every CN3UIString requests
		font.InitDeviceObjects(nullptr);
		font.RestoreDeviceObjects();

		if (!CDFont::HasUsableFont())
			GTEST_SKIP() << "no usable font file on this system";

		ASSERT_EQ(font.SetText("Hello"), S_OK);
		EXPECT_TRUE(font.IsSetText());

		// The laid-out text occupies real screen space...
		const SIZE size = font.GetSize();
		EXPECT_GT(size.cx, 0);
		EXPECT_GT(size.cy, 0);

		// ...the extent measure agrees for prefixes (word-wrap contract)...
		SIZE extent1 = {}, extent5 = {};
		EXPECT_TRUE(font.GetTextExtent("Hello", 1, &extent1));
		EXPECT_TRUE(font.GetTextExtent("Hello", 5, &extent5));
		EXPECT_GT(extent1.cx, 0);
		EXPECT_GT(extent5.cx, extent1.cx);
		EXPECT_GT(extent5.cy, 0);

		// ...and the atlas actually contains rasterized glyph pixels.
		IRHITexture* pTexture = font.GetRHITexture();
		ASSERT_NE(pTexture, nullptr);
		EXPECT_GT(CountLitTexels(pTexture), 0);

		// Drawing issues a real draw call through the device.
		const int iDrawsBefore = device.DrawCallCount();
		EXPECT_EQ(font.DrawText(10.0f, 20.0f, 0xFF00FF00, 0), S_OK);
		EXPECT_EQ(device.DrawCallCount(), iDrawsBefore + 1);
		EXPECT_EQ(font.GetFontColor(), 0xFF00FF00u);

		// Clearing the text releases the atlas and disarms drawing.
		EXPECT_EQ(font.SetText(""), S_OK);
		EXPECT_FALSE(font.IsSetText());
		EXPECT_EQ(font.DrawText(0.0f, 0.0f, 0xFFFFFFFF, 0), S_OK);
	}

	CN3Base::RHIDeviceSet(nullptr);
}

TEST(DFontFTTest, MultilineProducesTallerLayout)
{
	RHIDeviceNull device;
	CN3Base::RHIDeviceSet(&device);

	{
		CDFont font("굴림", 16, 0);
		font.InitDeviceObjects(nullptr);
		font.RestoreDeviceObjects();

		if (!CDFont::HasUsableFont())
			GTEST_SKIP() << "no usable font file on this system";

		ASSERT_EQ(font.SetText("line one"), S_OK);
		const SIZE sizeOneLine = font.GetSize();

		ASSERT_EQ(font.SetText("line one\nline two"), S_OK);
		const SIZE sizeTwoLines = font.GetSize();

		EXPECT_GT(sizeTwoLines.cy, sizeOneLine.cy);
	}

	CN3Base::RHIDeviceSet(nullptr);
}
