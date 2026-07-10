// POSIX DFont stub contract (docs/PORT_POSIX_PLAN.md, T6.8): until the
// FreeType backend lands (T7.1), DFont renders no text but must stay a safe
// no-op so the UI layer (N3UIString/N3UIBase) works. These checks pin that
// contract on the platforms that use the stub.

#include <gtest/gtest.h>

#include <N3Base/DFont.h>
#include <N3Base/RHI/RHIDeviceNull.h>

TEST(DFontStubTest, NoOpTextIsSafe)
{
	RHIDeviceNull device;
	CN3Base::RHIDeviceSet(&device);

	{
		CDFont font("Arial", 16, 0);
		EXPECT_EQ(font.GetFontName(), "Arial");
		EXPECT_EQ(font.GetFontHeight(), 16u);

		// Device object lifecycle succeeds without a real device.
		EXPECT_EQ(font.InitDeviceObjects(nullptr), S_OK);
		EXPECT_EQ(font.RestoreDeviceObjects(), S_OK);

		// No glyph atlas yet: stays "unset" so callers skip text drawing.
		EXPECT_EQ(font.SetText("hello"), S_OK);
		EXPECT_FALSE(font.IsSetText());

		// Drawing is a safe no-op.
		EXPECT_EQ(font.DrawText(10.0f, 10.0f, 0xFFFFFFFF, 0), S_OK);

		// Extent reports the font height with zero width.
		SIZE size = {};
		EXPECT_TRUE(font.GetTextExtent("hello", 5, &size));
		EXPECT_EQ(size.cx, 0);
		EXPECT_EQ(size.cy, 16);

		EXPECT_EQ(font.SetFontColor(0xFF00FF00), S_OK);
		EXPECT_EQ(font.GetFontColor(), 0xFF00FF00u);

		EXPECT_EQ(font.InvalidateDeviceObjects(), S_OK);
		EXPECT_EQ(font.DeleteDeviceObjects(), S_OK);
	}

	CN3Base::RHIDeviceSet(nullptr);
}
