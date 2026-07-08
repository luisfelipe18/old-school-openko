// SDL implementation of CLocalInput (POSIX platforms).
//
// The game identifies keys by DirectInput DIK_* scancodes everywhere
// (~150 call sites), so this backend keeps that contract: SDL's keyboard
// state is translated through a scancode map each tick and the press/release
// edge detection matches the DirectInput implementation exactly.

#include "StdAfx.h"
#include "LocalInput.h"
#include "LocalInputSDL.h"

#include <Platform/PlatformTime.h>

#include <SDL3/SDL.h>

namespace
{
// Milliseconds; matches the Windows default returned by GetDoubleClickTime().
constexpr uint32_t DOUBLE_CLICK_MS = 500;

// SDL_Scancode -> DIK_* map, built once. SDL scancodes are USB-HID based and
// stable across platforms.
struct ScancodeMapping
{
	SDL_Scancode sdl;
	int dik;
};

constexpr ScancodeMapping SCANCODE_MAPPINGS[] = {
	{SDL_SCANCODE_ESCAPE, DIK_ESCAPE},
	{SDL_SCANCODE_1, DIK_1},
	{SDL_SCANCODE_2, DIK_2},
	{SDL_SCANCODE_3, DIK_3},
	{SDL_SCANCODE_4, DIK_4},
	{SDL_SCANCODE_5, DIK_5},
	{SDL_SCANCODE_6, DIK_6},
	{SDL_SCANCODE_7, DIK_7},
	{SDL_SCANCODE_8, DIK_8},
	{SDL_SCANCODE_9, DIK_9},
	{SDL_SCANCODE_0, DIK_0},
	{SDL_SCANCODE_MINUS, DIK_MINUS},
	{SDL_SCANCODE_EQUALS, DIK_EQUALS},
	{SDL_SCANCODE_BACKSPACE, DIK_BACK},
	{SDL_SCANCODE_TAB, DIK_TAB},
	{SDL_SCANCODE_Q, DIK_Q},
	{SDL_SCANCODE_W, DIK_W},
	{SDL_SCANCODE_E, DIK_E},
	{SDL_SCANCODE_R, DIK_R},
	{SDL_SCANCODE_T, DIK_T},
	{SDL_SCANCODE_Y, DIK_Y},
	{SDL_SCANCODE_U, DIK_U},
	{SDL_SCANCODE_I, DIK_I},
	{SDL_SCANCODE_O, DIK_O},
	{SDL_SCANCODE_P, DIK_P},
	{SDL_SCANCODE_LEFTBRACKET, DIK_LBRACKET},
	{SDL_SCANCODE_RIGHTBRACKET, DIK_RBRACKET},
	{SDL_SCANCODE_RETURN, DIK_RETURN},
	{SDL_SCANCODE_LCTRL, DIK_LCONTROL},
	{SDL_SCANCODE_A, DIK_A},
	{SDL_SCANCODE_S, DIK_S},
	{SDL_SCANCODE_D, DIK_D},
	{SDL_SCANCODE_F, DIK_F},
	{SDL_SCANCODE_G, DIK_G},
	{SDL_SCANCODE_H, DIK_H},
	{SDL_SCANCODE_J, DIK_J},
	{SDL_SCANCODE_K, DIK_K},
	{SDL_SCANCODE_L, DIK_L},
	{SDL_SCANCODE_SEMICOLON, DIK_SEMICOLON},
	{SDL_SCANCODE_APOSTROPHE, DIK_APOSTROPHE},
	{SDL_SCANCODE_GRAVE, DIK_GRAVE},
	{SDL_SCANCODE_LSHIFT, DIK_LSHIFT},
	{SDL_SCANCODE_BACKSLASH, DIK_BACKSLASH},
	{SDL_SCANCODE_Z, DIK_Z},
	{SDL_SCANCODE_X, DIK_X},
	{SDL_SCANCODE_C, DIK_C},
	{SDL_SCANCODE_V, DIK_V},
	{SDL_SCANCODE_B, DIK_B},
	{SDL_SCANCODE_N, DIK_N},
	{SDL_SCANCODE_M, DIK_M},
	{SDL_SCANCODE_COMMA, DIK_COMMA},
	{SDL_SCANCODE_PERIOD, DIK_PERIOD},
	{SDL_SCANCODE_SLASH, DIK_SLASH},
	{SDL_SCANCODE_RSHIFT, DIK_RSHIFT},
	{SDL_SCANCODE_KP_MULTIPLY, DIK_MULTIPLY},
	{SDL_SCANCODE_LALT, DIK_LMENU},
	{SDL_SCANCODE_SPACE, DIK_SPACE},
	{SDL_SCANCODE_CAPSLOCK, DIK_CAPITAL},
	{SDL_SCANCODE_F1, DIK_F1},
	{SDL_SCANCODE_F2, DIK_F2},
	{SDL_SCANCODE_F3, DIK_F3},
	{SDL_SCANCODE_F4, DIK_F4},
	{SDL_SCANCODE_F5, DIK_F5},
	{SDL_SCANCODE_F6, DIK_F6},
	{SDL_SCANCODE_F7, DIK_F7},
	{SDL_SCANCODE_F8, DIK_F8},
	{SDL_SCANCODE_F9, DIK_F9},
	{SDL_SCANCODE_F10, DIK_F10},
	{SDL_SCANCODE_NUMLOCKCLEAR, DIK_NUMLOCK},
	{SDL_SCANCODE_SCROLLLOCK, DIK_SCROLL},
	{SDL_SCANCODE_KP_7, DIK_NUMPAD7},
	{SDL_SCANCODE_KP_8, DIK_NUMPAD8},
	{SDL_SCANCODE_KP_9, DIK_NUMPAD9},
	{SDL_SCANCODE_KP_MINUS, DIK_SUBTRACT},
	{SDL_SCANCODE_KP_4, DIK_NUMPAD4},
	{SDL_SCANCODE_KP_5, DIK_NUMPAD5},
	{SDL_SCANCODE_KP_6, DIK_NUMPAD6},
	{SDL_SCANCODE_KP_PLUS, DIK_ADD},
	{SDL_SCANCODE_KP_1, DIK_NUMPAD1},
	{SDL_SCANCODE_KP_2, DIK_NUMPAD2},
	{SDL_SCANCODE_KP_3, DIK_NUMPAD3},
	{SDL_SCANCODE_KP_0, DIK_NUMPAD0},
	{SDL_SCANCODE_KP_PERIOD, DIK_DECIMAL},
	{SDL_SCANCODE_NONUSBACKSLASH, DIK_OEM_102},
	{SDL_SCANCODE_F11, DIK_F11},
	{SDL_SCANCODE_F12, DIK_F12},
	{SDL_SCANCODE_F13, DIK_F13},
	{SDL_SCANCODE_F14, DIK_F14},
	{SDL_SCANCODE_F15, DIK_F15},
	{SDL_SCANCODE_INTERNATIONAL2, DIK_KANA},
	{SDL_SCANCODE_INTERNATIONAL1, DIK_ABNT_C1},
	{SDL_SCANCODE_INTERNATIONAL4, DIK_CONVERT},
	{SDL_SCANCODE_INTERNATIONAL5, DIK_NOCONVERT},
	{SDL_SCANCODE_INTERNATIONAL3, DIK_YEN},
	{SDL_SCANCODE_KP_EQUALS, DIK_NUMPADEQUALS},
	{SDL_SCANCODE_KP_ENTER, DIK_NUMPADENTER},
	{SDL_SCANCODE_RCTRL, DIK_RCONTROL},
	{SDL_SCANCODE_MUTE, DIK_MUTE},
	{SDL_SCANCODE_KP_COMMA, DIK_NUMPADCOMMA},
	{SDL_SCANCODE_KP_DIVIDE, DIK_DIVIDE},
	{SDL_SCANCODE_PRINTSCREEN, DIK_SYSRQ},
	{SDL_SCANCODE_RALT, DIK_RMENU},
	{SDL_SCANCODE_PAUSE, DIK_PAUSE},
	{SDL_SCANCODE_HOME, DIK_HOME},
	{SDL_SCANCODE_UP, DIK_UP},
	{SDL_SCANCODE_PAGEUP, DIK_PRIOR},
	{SDL_SCANCODE_LEFT, DIK_LEFT},
	{SDL_SCANCODE_RIGHT, DIK_RIGHT},
	{SDL_SCANCODE_END, DIK_END},
	{SDL_SCANCODE_DOWN, DIK_DOWN},
	{SDL_SCANCODE_PAGEDOWN, DIK_NEXT},
	{SDL_SCANCODE_INSERT, DIK_INSERT},
	{SDL_SCANCODE_DELETE, DIK_DELETE},
	{SDL_SCANCODE_LGUI, DIK_LWIN},
	{SDL_SCANCODE_RGUI, DIK_RWIN},
	{SDL_SCANCODE_APPLICATION, DIK_APPS},
	{SDL_SCANCODE_POWER, DIK_POWER},
	{SDL_SCANCODE_SLEEP, DIK_SLEEP},
};

// Dense lookup table: SDL scancode -> DIK code (0 = unmapped).
struct ScancodeTable
{
	int dikByScancode[SDL_SCANCODE_COUNT] = {};

	constexpr ScancodeTable()
	{
		for (const ScancodeMapping& mapping : SCANCODE_MAPPINGS)
			dikByScancode[mapping.sdl] = mapping.dik;
	}
};

constexpr ScancodeTable SCANCODE_TABLE;
} // namespace

int SdlScancodeToDik(int sdlScancode)
{
	if (sdlScancode < 0 || sdlScancode >= SDL_SCANCODE_COUNT)
		return 0;

	return SCANCODE_TABLE.dikByScancode[sdlScancode];
}

CLocalInput::CLocalInput()
{
	m_bKeyboardActive = true;

	m_hWnd            = nullptr;

	m_bNoKeyDown      = FALSE;

	m_nMouseFlag      = 0;
	m_nMouseFlagOld   = 0;

	m_dwTickLBDown    = 0;
	m_dwTickRBDown    = 0;

	m_ptCurMouse.x = m_ptCurMouse.y = 0;
	m_ptOldMouse.x = m_ptOldMouse.y = 0;

	m_rcLBDrag = {};
	m_rcMBDrag = {};
	m_rcRBDrag = {};

	memset(m_byCurKeys, 0, sizeof(m_byCurKeys));
	memset(m_byOldKeys, 0, sizeof(m_byOldKeys));
	memset(m_bKeyPresses, 0, sizeof(m_bKeyPresses));
	memset(m_bKeyPresseds, 0, sizeof(m_bKeyPresseds));
	memset(m_dwTickKeyPress, 0, sizeof(m_dwTickKeyPress));
}

CLocalInput::~CLocalInput()
{
}

BOOL CLocalInput::Init(HINSTANCE /*hInst*/, HWND hWnd)
{
	m_hWnd = hWnd; // legacy handle, unused by the SDL backend

	AcquireKeyboard();
	return TRUE;
}

void CLocalInput::SetActiveDevices(BOOL bKeyboard)
{
	if (bKeyboard)
		AcquireKeyboard();
	else
		UnacquireKeyboard();
}

void CLocalInput::KeyboardFlushData()
{
	memset(m_byOldKeys, 0, NUMDIKEYS);
	memset(m_byCurKeys, 0, NUMDIKEYS);
}

void CLocalInput::MouseSetPos(int x, int y)
{
	m_ptCurMouse.x = x;
	m_ptCurMouse.y = y;
}

BOOL CLocalInput::KeyboardGetKeyState(int nDIKey)
{
	if (nDIKey < 0 || nDIKey >= NUMDIKEYS)
		return FALSE;

	return m_byCurKeys[nDIKey];
}

void CLocalInput::AcquireKeyboard()
{
	m_bKeyboardActive = true;
	KeyboardFlushData();
}

void CLocalInput::UnacquireKeyboard()
{
	KeyboardFlushData();
	m_bKeyboardActive = false;
}

void CLocalInput::Tick()
{
	// Only sample while one of our windows has keyboard focus (the
	// DirectInput backend compares GetActiveWindow() against the game window).
	if (SDL_GetKeyboardFocus() == nullptr)
		return;

	///////////////////////
	//  KEYBOARD
	///////////////////////
	memcpy(m_byOldKeys, m_byCurKeys, NUMDIKEYS); // 전의 키 상태 기록

	if (m_bKeyboardActive)
	{
		int numSdlKeys        = 0;
		const bool* sdlKeys   = SDL_GetKeyboardState(&numSdlKeys);

		memset(m_byCurKeys, 0, NUMDIKEYS);
		for (int scancode = 0; scancode < numSdlKeys && scancode < SDL_SCANCODE_COUNT; ++scancode)
		{
			if (!sdlKeys[scancode])
				continue;

			const int dik = SCANCODE_TABLE.dikByScancode[scancode];
			if (dik != 0)
				m_byCurKeys[dik] = 0x80; // DirectInput reports the high bit for held keys
		}

		m_bNoKeyDown = TRUE; // 첨엔 아무것도 안눌림

		for (int i = 0; i < NUMDIKEYS; i++)
		{
			if (!m_byOldKeys[i] && m_byCurKeys[i])
				m_bKeyPresses[i] = TRUE; // 눌리는 순간
			else
				m_bKeyPresses[i] = FALSE;

			if (m_byOldKeys[i] && !m_byCurKeys[i])
				m_bKeyPresseds[i] = TRUE; // 눌렀다 떼는 순간..
			else
				m_bKeyPresseds[i] = FALSE;

			if (m_byCurKeys[i])
				m_bNoKeyDown = FALSE;
		}
	}

	///////////////////////
	//  MOUSE
	///////////////////////

	m_ptOldMouse = m_ptCurMouse; // 일단 전의 것 복사...

	float fMouseX = 0.0f, fMouseY = 0.0f;
	const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&fMouseX, &fMouseY);

	m_ptCurMouse.x = static_cast<LONG>(fMouseX);
	m_ptCurMouse.y = static_cast<LONG>(fMouseY);

	// SDL reports coordinates relative to the mouse-focus window; no focus
	// means the cursor is outside the client area (PtInRect equivalent).
	if (SDL_GetMouseFocus() != nullptr)
	{
		// 마우스 버튼 상태 보관.
		m_nMouseFlagOld = m_nMouseFlag;
		m_nMouseFlag    = 0;

		// 마우스 상태 가져오기
		if (buttons & SDL_BUTTON_LMASK)
			m_nMouseFlag |= MOUSE_LBDOWN;

		if (buttons & SDL_BUTTON_MMASK)
			m_nMouseFlag |= MOUSE_MBDOWN;

		if (buttons & SDL_BUTTON_RMASK)
			m_nMouseFlag |= MOUSE_RBDOWN;

		// 버튼 클릭 직후..
		if (!(m_nMouseFlagOld & MOUSE_LBDOWN) && (m_nMouseFlag & MOUSE_LBDOWN))
			m_nMouseFlag |= MOUSE_LBCLICK;

		if (!(m_nMouseFlagOld & MOUSE_MBDOWN) && (m_nMouseFlag & MOUSE_MBDOWN))
			m_nMouseFlag |= MOUSE_MBCLICK;

		if (!(m_nMouseFlagOld & MOUSE_RBDOWN) && (m_nMouseFlag & MOUSE_RBDOWN))
			m_nMouseFlag |= MOUSE_RBCLICK;

		// 버튼에서 손을 떼면
		if ((m_nMouseFlagOld & MOUSE_LBDOWN) && !(m_nMouseFlag & MOUSE_LBDOWN))
			m_nMouseFlag |= MOUSE_LBCLICKED;

		if ((m_nMouseFlagOld & MOUSE_MBDOWN) && !(m_nMouseFlag & MOUSE_MBDOWN))
			m_nMouseFlag |= MOUSE_MBCLICKED;

		if ((m_nMouseFlagOld & MOUSE_RBDOWN) && !(m_nMouseFlag & MOUSE_RBDOWN))
			m_nMouseFlag |= MOUSE_RBCLICKED;

		if (m_nMouseFlag & MOUSE_LBCLICKED) // 왼쪽 더블 클릭 감지
		{
			static uint32_t dwCLicked = 0;
			if (PlatformTickMs() < dwCLicked + DOUBLE_CLICK_MS)
				m_nMouseFlag |= MOUSE_LBDBLCLK;
			dwCLicked = PlatformTickMs();
		}

		if (m_nMouseFlag & MOUSE_MBCLICKED)
		{
			static uint32_t dwCLicked = 0;
			if (PlatformTickMs() < dwCLicked + DOUBLE_CLICK_MS)
				m_nMouseFlag |= MOUSE_MBDBLCLK;
			dwCLicked = PlatformTickMs();
		}

		if (m_nMouseFlag & MOUSE_RBCLICKED)
		{
			static uint32_t dwCLicked = 0;
			if (PlatformTickMs() < dwCLicked + DOUBLE_CLICK_MS)
				m_nMouseFlag |= MOUSE_RBDBLCLK;
			dwCLicked = PlatformTickMs();
		}

		// 드래그 영역 처리
		if (m_nMouseFlag & MOUSE_LBDOWN)
		{
			m_rcLBDrag.right  = m_ptCurMouse.x;
			m_rcLBDrag.bottom = m_ptCurMouse.y;
		}

		if (m_nMouseFlag & MOUSE_MBDOWN)
		{
			m_rcMBDrag.right  = m_ptCurMouse.x;
			m_rcMBDrag.bottom = m_ptCurMouse.y;
		}

		if (m_nMouseFlag & MOUSE_RBDOWN)
		{
			m_rcRBDrag.right  = m_ptCurMouse.x;
			m_rcRBDrag.bottom = m_ptCurMouse.y;
		}

		if (m_nMouseFlag & MOUSE_LBCLICK)
		{
			m_rcLBDrag.left = m_ptCurMouse.x;
			m_rcLBDrag.top  = m_ptCurMouse.y;
		}

		if (m_nMouseFlag & MOUSE_MBCLICK)
		{
			m_rcMBDrag.left = m_ptCurMouse.x;
			m_rcMBDrag.top  = m_ptCurMouse.y;
		}

		if (m_nMouseFlag & MOUSE_RBCLICK)
		{
			m_rcRBDrag.left = m_ptCurMouse.x;
			m_rcRBDrag.top  = m_ptCurMouse.y;
		}
	}
}
