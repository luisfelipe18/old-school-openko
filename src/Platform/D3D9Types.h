#ifndef PLATFORM_D3D9TYPES_H
#define PLATFORM_D3D9TYPES_H

#pragma once

// Portable definitions of the Direct3D 9 *data* types the engine embeds in
// its own structures and file formats (see docs/PORT_POSIX_PLAN.md, phase 1).
//
// On Windows this header defines nothing: the real <d3d9types.h> definitions
// are used, so the existing build is preserved exactly. On POSIX platforms
// these definitions must stay binary-compatible with Direct3D 9's, because
// the game's asset formats serialize some of them directly (e.g. D3DCOLOR
// vertex colors, D3DFORMAT tags in texture headers). Layout is enforced by
// static_asserts in N3Base/My_3DStruct.cpp.
//
// Interface types (IDirect3DDevice9, textures, buffers, ...) are declared as
// opaque pointers only - the POSIX build never calls through them; rendering
// goes through the RHI introduced in later phases.

#ifndef _WIN32

#include <Platform/PlatformTypes.h>

// --- Opaque COM interface handles ------------------------------------------

struct IDirect3D9;
struct IDirect3DDevice9;
struct IDirect3DBaseTexture9;
struct IDirect3DTexture9;
struct IDirect3DSurface9;
struct IDirect3DVertexBuffer9;
struct IDirect3DIndexBuffer9;

using LPDIRECT3D9             = IDirect3D9*;
using LPDIRECT3DDEVICE9       = IDirect3DDevice9*;
using LPDIRECT3DBASETEXTURE9  = IDirect3DBaseTexture9*;
using LPDIRECT3DTEXTURE9      = IDirect3DTexture9*;
using LPDIRECT3DSURFACE9      = IDirect3DSurface9*;
using LPDIRECT3DVERTEXBUFFER9 = IDirect3DVertexBuffer9*;
using LPDIRECT3DINDEXBUFFER9  = IDirect3DIndexBuffer9*;

// --- Plain data types --------------------------------------------------------

// D3DCOLOR is 32-bit ARGB (0xAARRGGBB).
using D3DCOLOR = DWORD;

#ifndef D3DCOLOR_ARGB
#define D3DCOLOR_ARGB(a, r, g, b) \
	((D3DCOLOR) ((((a) & 0xff) << 24) | (((r) & 0xff) << 16) | (((g) & 0xff) << 8) | ((b) & 0xff)))
#define D3DCOLOR_RGBA(r, g, b, a) D3DCOLOR_ARGB(a, r, g, b)
#define D3DCOLOR_XRGB(r, g, b)    D3DCOLOR_ARGB(0xff, r, g, b)
#endif

typedef struct _D3DCOLORVALUE
{
	float r;
	float g;
	float b;
	float a;
} D3DCOLORVALUE;

typedef struct _D3DVECTOR
{
	float x;
	float y;
	float z;
} D3DVECTOR;

typedef struct _D3DMATERIAL9
{
	D3DCOLORVALUE Diffuse;
	D3DCOLORVALUE Ambient;
	D3DCOLORVALUE Specular;
	D3DCOLORVALUE Emissive;
	float Power;
} D3DMATERIAL9;

typedef enum _D3DLIGHTTYPE
{
	D3DLIGHT_POINT       = 1,
	D3DLIGHT_SPOT        = 2,
	D3DLIGHT_DIRECTIONAL = 3,
	D3DLIGHT_FORCE_DWORD = 0x7fffffff
} D3DLIGHTTYPE;

typedef struct _D3DLIGHT9
{
	D3DLIGHTTYPE Type;
	D3DCOLORVALUE Diffuse;
	D3DCOLORVALUE Specular;
	D3DCOLORVALUE Ambient;
	D3DVECTOR Position;
	D3DVECTOR Direction;
	float Range;
	float Falloff;
	float Attenuation0;
	float Attenuation1;
	float Attenuation2;
	float Theta;
	float Phi;
} D3DLIGHT9;

typedef struct _D3DVIEWPORT9
{
	DWORD X;
	DWORD Y;
	DWORD Width;
	DWORD Height;
	float MinZ;
	float MaxZ;
} D3DVIEWPORT9;

// Values must match d3d9types.h: asset files serialize D3DFORMAT tags.
typedef enum _D3DFORMAT
{
	D3DFMT_UNKNOWN     = 0,

	D3DFMT_R8G8B8      = 20,
	D3DFMT_A8R8G8B8    = 21,
	D3DFMT_X8R8G8B8    = 22,
	D3DFMT_R5G6B5      = 23,
	D3DFMT_X1R5G5B5    = 24,
	D3DFMT_A1R5G5B5    = 25,
	D3DFMT_A4R4G4B4    = 26,

	D3DFMT_DXT1        = MAKEFOURCC('D', 'X', 'T', '1'),
	D3DFMT_DXT2        = MAKEFOURCC('D', 'X', 'T', '2'),
	D3DFMT_DXT3        = MAKEFOURCC('D', 'X', 'T', '3'),
	D3DFMT_DXT4        = MAKEFOURCC('D', 'X', 'T', '4'),
	D3DFMT_DXT5        = MAKEFOURCC('D', 'X', 'T', '5'),

	D3DFMT_D16_LOCKABLE = 70,
	D3DFMT_D32          = 71,
	D3DFMT_D15S1        = 73,
	D3DFMT_D24S8        = 75,
	D3DFMT_D24X8        = 77,
	D3DFMT_D24X4S4      = 79,
	D3DFMT_D16          = 80,

	D3DFMT_INDEX16      = 101,
	D3DFMT_INDEX32      = 102,

	D3DFMT_FORCE_DWORD  = 0x7fffffff
} D3DFORMAT;

typedef enum _D3DPRIMITIVETYPE
{
	D3DPT_POINTLIST     = 1,
	D3DPT_LINELIST      = 2,
	D3DPT_LINESTRIP     = 3,
	D3DPT_TRIANGLELIST  = 4,
	D3DPT_TRIANGLESTRIP = 5,
	D3DPT_TRIANGLEFAN   = 6,
	D3DPT_FORCE_DWORD   = 0x7fffffff
} D3DPRIMITIVETYPE;

typedef enum _D3DPOOL
{
	D3DPOOL_DEFAULT     = 0,
	D3DPOOL_MANAGED     = 1,
	D3DPOOL_SYSTEMMEM   = 2,
	D3DPOOL_SCRATCH     = 3,
	D3DPOOL_FORCE_DWORD = 0x7fffffff
} D3DPOOL;

typedef enum _D3DMULTISAMPLE_TYPE
{
	D3DMULTISAMPLE_NONE        = 0,
	D3DMULTISAMPLE_FORCE_DWORD = 0x7fffffff
} D3DMULTISAMPLE_TYPE;

typedef enum _D3DSWAPEFFECT
{
	D3DSWAPEFFECT_DISCARD     = 1,
	D3DSWAPEFFECT_FLIP        = 2,
	D3DSWAPEFFECT_COPY        = 3,
	D3DSWAPEFFECT_FORCE_DWORD = 0x7fffffff
} D3DSWAPEFFECT;

typedef enum _D3DDEVTYPE
{
	D3DDEVTYPE_HAL         = 1,
	D3DDEVTYPE_REF         = 2,
	D3DDEVTYPE_SW          = 3,
	D3DDEVTYPE_FORCE_DWORD = 0x7fffffff
} D3DDEVTYPE;

typedef struct _D3DPRESENT_PARAMETERS_
{
	UINT BackBufferWidth;
	UINT BackBufferHeight;
	D3DFORMAT BackBufferFormat;
	UINT BackBufferCount;
	D3DMULTISAMPLE_TYPE MultiSampleType;
	DWORD MultiSampleQuality;
	D3DSWAPEFFECT SwapEffect;
	HWND hDeviceWindow;
	BOOL Windowed;
	BOOL EnableAutoDepthStencil;
	D3DFORMAT AutoDepthStencilFormat;
	DWORD Flags;
	UINT FullScreen_RefreshRateInHz;
	UINT PresentationInterval;
} D3DPRESENT_PARAMETERS;

// Subset of D3DCAPS9 covering the fields the engine actually reads.
typedef struct _D3DCAPS9
{
	D3DDEVTYPE DeviceType;
	DWORD TextureCaps;
	DWORD PrimitiveMiscCaps;
	DWORD MaxTextureWidth;
	DWORD MaxTextureHeight;
	DWORD MaxTextureBlendStages;
	DWORD MaxSimultaneousTextures;
} D3DCAPS9;

// D3DTEXTURECAPS_* flag values (match d3d9caps.h).
#ifndef D3DPTEXTURECAPS_POW2
#define D3DPTEXTURECAPS_POW2       0x00000002L
#define D3DPTEXTURECAPS_SQUAREONLY 0x00000020L
#define D3DPTEXTURECAPS_MIPMAP     0x00004000L
#endif

typedef enum _D3DBLEND
{
	D3DBLEND_ZERO         = 1,
	D3DBLEND_ONE          = 2,
	D3DBLEND_SRCCOLOR     = 3,
	D3DBLEND_INVSRCCOLOR  = 4,
	D3DBLEND_SRCALPHA     = 5,
	D3DBLEND_INVSRCALPHA  = 6,
	D3DBLEND_DESTALPHA    = 7,
	D3DBLEND_INVDESTALPHA = 8,
	D3DBLEND_DESTCOLOR    = 9,
	D3DBLEND_INVDESTCOLOR = 10,
	D3DBLEND_SRCALPHASAT  = 11,
	D3DBLEND_FORCE_DWORD  = 0x7fffffff
} D3DBLEND;

typedef enum _D3DTEXTUREOP
{
	D3DTOP_DISABLE     = 1,
	D3DTOP_SELECTARG1  = 2,
	D3DTOP_SELECTARG2  = 3,
	D3DTOP_MODULATE    = 4,
	D3DTOP_MODULATE2X  = 5,
	D3DTOP_MODULATE4X  = 6,
	D3DTOP_ADD         = 7,
	D3DTOP_FORCE_DWORD = 0x7fffffff
} D3DTEXTUREOP;

// Texture-stage argument tokens (match d3d9types.h).
#ifndef D3DTA_DIFFUSE
#define D3DTA_DIFFUSE  0x00000000
#define D3DTA_CURRENT  0x00000001
#define D3DTA_TEXTURE  0x00000002
#define D3DTA_TFACTOR  0x00000003
#define D3DTA_SPECULAR 0x00000004
#endif

// --- Flexible Vertex Format bits (match d3d9types.h) -------------------------

#ifndef D3DFVF_XYZ
#define D3DFVF_XYZ      0x002
#define D3DFVF_XYZRHW   0x004
#define D3DFVF_NORMAL   0x010
#define D3DFVF_PSIZE    0x020
#define D3DFVF_DIFFUSE  0x040
#define D3DFVF_SPECULAR 0x080
#define D3DFVF_TEX0     0x000
#define D3DFVF_TEX1     0x100
#define D3DFVF_TEX2     0x200
#define D3DFVF_TEX3     0x300
#endif

#endif // !_WIN32

#endif // PLATFORM_D3D9TYPES_H
