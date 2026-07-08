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

#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3)                                          \
	((DWORD) (BYTE) (ch0) | ((DWORD) (BYTE) (ch1) << 8) | ((DWORD) (BYTE) (ch2) << 16) \
		| ((DWORD) (BYTE) (ch3) << 24))
#endif

#ifndef _MAX_PATH
#define _MAX_PATH 1024
#endif

#endif // !_WIN32

#endif // PLATFORM_PLATFORMTYPES_H
