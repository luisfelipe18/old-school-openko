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
#include <cstdlib>
#include <cstring>

using BOOL     = int;
using INT      = int;
using BYTE     = uint8_t;
// Win32 defines a global `byte` (unsigned char, from rpcndr.h) that legacy
// signatures use; mirror it so those headers parse. Kept distinct from
// std::byte, exactly as on Windows (code here never does `using namespace std`
// at global scope, so there is no ambiguity).
using byte     = unsigned char;
using WORD     = uint16_t;
using DWORD    = uint32_t;
using UINT     = uint32_t;
using LONG     = int32_t;
using ULONG    = uint32_t;
using LONGLONG = int64_t;
using FLOAT    = float;

using LPBYTE   = BYTE*;
using LPWORD   = WORD*;
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

// Win32 MessageBox style flags, reused by the game's own UI message box as its
// style enum. Values match winuser.h.
#ifndef MB_OK
#define MB_OK          0x00000000
#define MB_OKCANCEL    0x00000001
#define MB_YESNOCANCEL 0x00000003
#define MB_YESNO       0x00000004
#endif

// Win32 PostQuitMessage shim. There is no Win32 message loop on POSIX; instead
// the request is recorded in a process-global flag that the SDL entry point
// polls to break its loop. PlatformQuitRequested() is the query side.
inline bool& PlatformQuitFlag()
{
	static bool bQuit = false;
	return bQuit;
}
inline void PostQuitMessage(int /*nExitCode*/)
{
	PlatformQuitFlag() = true;
}
inline bool PlatformQuitRequested()
{
	return PlatformQuitFlag();
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

inline BOOL EqualRect(const RECT* r1, const RECT* r2)
{
	if (r1 == nullptr || r2 == nullptr)
		return FALSE;
	return r1->left == r2->left && r1->top == r2->top && r1->right == r2->right
		&& r1->bottom == r2->bottom;
}

// Win32 global-memory shims used by the terrain heightmap allocation. GMEM_FIXED
// returns a plain pointer (no HLOCAL/Lock dance), so malloc/free suffice.
#ifndef GMEM_FIXED
#define GMEM_FIXED    0x0000
#define GMEM_ZEROINIT 0x0040
#endif
inline void* GlobalAlloc(UINT uFlags, size_t dwBytes)
{
	void* p = std::malloc(dwBytes);
	if (p != nullptr && (uFlags & GMEM_ZEROINIT) != 0)
		std::memset(p, 0, dwBytes);
	return p;
}
inline void* GlobalFree(void* hMem)
{
	std::free(hMem);
	return nullptr;
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
