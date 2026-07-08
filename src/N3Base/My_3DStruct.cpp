#include "StdAfxBase.h"
#include "My_3DStruct.h"

#include "ColorValue.inl"
#include "Material.inl"
#include "VertexTypes.inl"

D3DCOLOR _RGB_To_D3DCOLOR(COLORREF cr, uint32_t dwAlpha)
{
	D3DCOLOR cr2 = (dwAlpha << 24)              // A
				   | ((cr & 0x000000ff) << 16)  // R
				   | (cr & 0x0000ff00)          // G
				   | ((cr & 0x00ff0000) >> 16); // B
	return cr2;
}

COLORREF _D3DCOLOR_To_RGB(D3DCOLOR cr)
{
	COLORREF cr2 = ((cr & 0x00ff0000) >> 16)    // R
				   | (cr & 0x0000ff00)          // G
				   | ((cr & 0x000000ff) << 16); // B
	return cr2;
}

COLORREF _D3DCOLORVALUE_To_RGB(const D3DCOLORVALUE& cr)
{
	COLORREF cr2 = (((uint32_t) (cr.r * 255.0f)))          // R
				   | (((uint32_t) (cr.g * 255.0f)) << 8)   // G
				   | (((uint32_t) (cr.b * 255.0f)) << 16); // B
	return cr2;
}

D3DCOLOR _D3DCOLORVALUE_To_D3DCOLOR(const D3DCOLORVALUE& cr)
{
	COLORREF cr2 = (((uint32_t) (cr.a * 255.0f)) << 24)   // A
				   | (((uint32_t) (cr.r * 255.0f)) << 16) // R
				   | (((uint32_t) (cr.g * 255.0f)) << 8)  // G
				   | (((uint32_t) (cr.b * 255.0f)));      // B
	return cr2;
}

D3DCOLORVALUE _RGB_To_D3DCOLORVALUE(COLORREF cr, float fAlpha)
{
	D3DCOLORVALUE cr2;
	cr2.a = fAlpha; // Alpha
	cr2.r = (cr & 0x000000ff) / 255.0f;
	cr2.g = ((cr & 0x0000ff00) >> 8) / 255.0f;
	cr2.b = ((cr & 0x00ff0000) >> 16) / 255.0f;
	return cr2;
}

D3DCOLORVALUE _D3DCOLOR_To_D3DCOLORVALUE(D3DCOLOR cr)
{
	D3DCOLORVALUE cr2;
	cr2.a = ((cr & 0xff000000) >> 24) / 255.0f;
	cr2.r = ((cr & 0x00ff0000) >> 16) / 255.0f;
	cr2.g = ((cr & 0x0000ff00) >> 8) / 255.0f;
	cr2.b = (cr & 0x000000ff) / 255.0f; // Alpha
	return cr2;
}

#ifdef _WIN32
int16_t _IsKeyDown(int iVirtualKey)
{
	return (GetAsyncKeyState(iVirtualKey) & 0xff00);
}

int16_t _IsKeyDowned(int iVirtualKey)
{
	return (GetAsyncKeyState(iVirtualKey) & 0x00ff);
}
#else
// POSIX port: keyboard state comes back through SDL in the input phase
// (docs/PORT_POSIX_PLAN.md, phase 3); until then no key is ever down.
int16_t _IsKeyDown(int /*iVirtualKey*/)
{
	return 0;
}

int16_t _IsKeyDowned(int /*iVirtualKey*/)
{
	return 0;
}
#endif // _WIN32

// The vertex/material/light structures are serialized directly into the
// game's asset formats and, on POSIX platforms, are built from our own
// Direct3D data-type definitions (Platform/D3D9Types.h). Freeze their layout
// on every platform so a divergence is a compile error rather than silent
// asset corruption.
static_assert(sizeof(D3DCOLOR) == 4);
static_assert(sizeof(D3DCOLORVALUE) == 16);
static_assert(sizeof(D3DVECTOR) == 12);
static_assert(sizeof(__Vector3) == 12);
static_assert(sizeof(__ColorValue) == 16);
static_assert(sizeof(__Matrix44) == 64);
static_assert(sizeof(_D3DMATERIAL9) == 68);
static_assert(sizeof(__VertexColor) == 16);
static_assert(sizeof(__VertexTransformedColor) == 20);
static_assert(sizeof(__VertexT1) == 32);
static_assert(sizeof(__VertexT2) == 40);
static_assert(sizeof(__VertexTransformed) == 28);
static_assert(sizeof(__VertexTransformedT2) == 36);
static_assert(sizeof(__VertexXyzT1) == 20);
static_assert(sizeof(__VertexXyzT2) == 28);
static_assert(sizeof(__VertexXyzNormal) == 24);
static_assert(sizeof(__VertexXyzColorT1) == 24);
static_assert(sizeof(__VertexXyzColorT2) == 32);
static_assert(sizeof(__VertexXyzColor) == 16);
static_assert(sizeof(__VertexXyzNormalColor) == 28);
