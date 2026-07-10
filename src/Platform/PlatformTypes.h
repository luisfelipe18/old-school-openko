#ifndef PLATFORM_PLATFORMTYPES_H
#define PLATFORM_PLATFORMTYPES_H

#pragma once

// Portable definitions of the Win32 *data* types that leak through the
// engine's interfaces (POINT/RECT in UI code, BOOL/DWORD in legacy signatures,
// opaque handles held by classes that are compiled but not yet exercised on
// POSIX platforms). See docs/PORT_POSIX_PLAN.md (phase 1).
//
// On Windows this header intentionally defines nothing: the real types come
// from <windows.h> via the usual include chains, preserving the existing
// build exactly. Only Win32 *data* is shimmed here - functions (windowing,
// GDI, sockets, ...) are ported in later phases, not emulated.

#ifndef _WIN32

#include <cstdint>
#include <cstring>

using BOOL     = int;
using INT      = int;
using BYTE     = uint8_t;
using WORD     = uint16_t;
using DWORD    = uint32_t;
using UINT     = uint32_t;
using LONG     = int32_t;
using ULONG    = uint32_t;
using LONGLONG = int64_t;
using FLOAT    = float;

using LPBYTE   = BYTE*;
using LPDWORD  = DWORD*;
using HRESULT  = LONG;

#ifndef ZeroMemory
#define ZeroMemory(dest, len)      std::memset((dest), 0, (len))
#define CopyMemory(dest, src, len) std::memcpy((dest), (src), (len))
#endif

// COM result helpers (match winerror.h semantics).
#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT) (hr)) >= 0)
#define FAILED(hr)    (((HRESULT) (hr)) < 0)
#define S_OK          ((HRESULT) 0)
#define S_FALSE       ((HRESULT) 1)
#define E_FAIL        ((HRESULT) 0x80004005L)
#define E_OUTOFMEMORY ((HRESULT) 0x8007000EL)
#endif

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

using HANDLE = void*;

// Opaque window-system handles. Kept as distinct pointer types so signatures
// still typecheck; they are never dereferenced on POSIX platforms.
struct HWND__;
using HWND = HWND__*;
struct HINSTANCE__;
using HINSTANCE = HINSTANCE__*;

// GDI handles used by the (POSIX-stubbed) DFont text layer; never dereferenced.
using HDC   = void*;
using HFONT = void*;

// Win32 cursor handle held (but not dereferenced) by the game-procedure and
// cursor layers until the SDL cursor path lands. Kept as an opaque pointer so
// the legacy signatures still typecheck on POSIX.
using HCURSOR = void*;

// GetDeviceCaps index used by DFont; only LOGPIXELSY is referenced.
#ifndef LOGPIXELSY
#define LOGPIXELSY 90
#endif

// Win32 helpers DFont's inline sizing uses. 96 DPI is the classic default.
inline int GetDeviceCaps(HDC /*hDC*/, int /*index*/)
{
	return 96;
}

inline int MulDiv(int nNumber, int nNumerator, int nDenominator)
{
	if (nDenominator == 0)
		return -1;
	return static_cast<int>(
		(static_cast<long long>(nNumber) * nNumerator + nDenominator / 2) / nDenominator);
}

// Win32 LANGID used to pick the language-suffixed .tbl files. POSIX builds
// default to US English (0x0409); the Taiwan path (0x0404) is Windows-only.
#ifndef LANG_ENGLISH_US
#define LANG_ENGLISH_US 0x0409
#endif
inline WORD GetUserDefaultLangID()
{
	return LANG_ENGLISH_US;
}

struct POINT
{
	LONG x;
	LONG y;
};

struct RECT
{
	LONG left;
	LONG top;
	LONG right;
	LONG bottom;
};

struct SIZE
{
	LONG cx;
	LONG cy;
};

// COLORREF is 0x00BBGGRR (note: *not* the same channel order as D3DCOLOR).
using COLORREF = DWORD;

#ifndef RGB
#define RGB(r, g, b) \
	((COLORREF) (((BYTE) (r)) | (((WORD) ((BYTE) (g))) << 8) | (((DWORD) ((BYTE) (b))) << 16)))
#define GetRValue(rgb) ((BYTE) (rgb))
#define GetGValue(rgb) ((BYTE) (((WORD) (rgb)) >> 8))
#define GetBValue(rgb) ((BYTE) ((rgb) >> 16))
#endif

// Small Win32 RECT helpers used by UI/render code.
inline BOOL SetRect(RECT* rc, LONG left, LONG top, LONG right, LONG bottom)
{
	if (rc == nullptr)
		return FALSE;
	rc->left   = left;
	rc->top    = top;
	rc->right  = right;
	rc->bottom = bottom;
	return TRUE;
}

inline BOOL SetRectEmpty(RECT* rc)
{
	return SetRect(rc, 0, 0, 0, 0);
}

inline BOOL PtInRect(const RECT* rc, POINT pt)
{
	if (rc == nullptr)
		return FALSE;
	return pt.x >= rc->left && pt.x < rc->right && pt.y >= rc->top && pt.y < rc->bottom;
}

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3)                                          \
	((DWORD) (BYTE) (ch0) | ((DWORD) (BYTE) (ch1) << 8) | ((DWORD) (BYTE) (ch2) << 16) \
		| ((DWORD) (BYTE) (ch3) << 24))
#endif

#ifndef _MAX_PATH
#define _MAX_PATH 1024
#endif

// Windows value (260): legacy structs size their name buffers with it.
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#endif // !_WIN32

#endif // PLATFORM_PLATFORMTYPES_H
