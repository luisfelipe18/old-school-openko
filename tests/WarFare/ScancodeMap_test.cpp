#include <gtest/gtest.h>

#include <LocalInputSDL.h>
#include <Platform/DInputKeyCodes.h>

#include <SDL3/SDL_scancode.h>

TEST(ScancodeMapTest, MapsRepresentativeKeys)
{
	// Movement/action keys the game logic queries constantly.
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_W), DIK_W);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_A), DIK_A);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_S), DIK_S);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_D), DIK_D);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_ESCAPE), DIK_ESCAPE);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_RETURN), DIK_RETURN);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_SPACE), DIK_SPACE);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_TAB), DIK_TAB);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_1), DIK_1);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_0), DIK_0);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_F1), DIK_F1);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_F12), DIK_F12);

	// Hotkey-table keys (GameDef.h) and modifiers.
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_LSHIFT), DIK_LSHIFT);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_RSHIFT), DIK_RSHIFT);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_LCTRL), DIK_LCONTROL);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_LALT), DIK_LMENU);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_RALT), DIK_RMENU);

	// Arrows/navigation (camera + UI).
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_UP), DIK_UP);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_DOWN), DIK_DOWN);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_LEFT), DIK_LEFT);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_RIGHT), DIK_RIGHT);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_PAGEUP), DIK_PRIOR);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_PAGEDOWN), DIK_NEXT);

	// Numpad (screenshot hotkey is NUM-, zoom uses +/-).
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_KP_MINUS), DIK_NUMPADMINUS);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_KP_PLUS), DIK_NUMPADPLUS);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_KP_5), DIK_NUMPAD5);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_KP_ENTER), DIK_NUMPADENTER);
}

TEST(ScancodeMapTest, UnmappedAndOutOfRangeScancodesReturnZero)
{
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_UNKNOWN), 0);
	EXPECT_EQ(SdlScancodeToDik(-1), 0);
	EXPECT_EQ(SdlScancodeToDik(SDL_SCANCODE_COUNT), 0);
	EXPECT_EQ(SdlScancodeToDik(1 << 20), 0);
}

TEST(ScancodeMapTest, NoTwoScancodesShareADikCodeExceptAliases)
{
	int dikSeen[256] = {};
	for (int scancode = 0; scancode < SDL_SCANCODE_COUNT; ++scancode)
	{
		const int dik = SdlScancodeToDik(scancode);
		if (dik == 0)
			continue;

		ASSERT_GE(dik, 0);
		ASSERT_LT(dik, 256);
		EXPECT_EQ(dikSeen[dik], 0) << "DIK 0x" << std::hex << dik << " mapped twice";
		dikSeen[dik] = 1;
	}
}
