#ifndef CLIENT_WARFARE_GLTRANSLATE_H
#define CLIENT_WARFARE_GLTRANSLATE_H

#pragma once

// Pure Direct3D 9 -> OpenGL translation helpers for the RHI GL backend
// (docs/PORT_POSIX_PLAN.md, T6.6/T6.7). Everything here is stateless and
// makes no GL calls, so it is unit-tested headlessly (tests/WarFare).

#include "GLLoader.h"

#include <N3Base/My_3DStruct.h>

namespace gltr
{

// --- FVF vertex layout --------------------------------------------------------

/// Byte offsets of each attribute inside an FVF vertex; -1 when absent.
/// Field order in the vertex follows D3D9: position (3 or 4 floats with RHW),
/// normal, diffuse (D3DCOLOR), specular (skipped), then 2-float UV sets.
struct FVFLayout
{
	int stride       = 0;
	bool xyzrhw      = false; // pre-transformed (screen-space) position
	int posOffset    = 0;     // always present (3 floats, +rhw when xyzrhw)
	int normalOffset = -1;
	int colorOffset  = -1;    // D3DCOLOR (BGRA byte order in memory)
	int uvOffset[2]  = {-1, -1};
	int uvCount      = 0;
	bool valid       = false;
};

inline FVFLayout ParseFVF(DWORD fvf)
{
	FVFLayout layout;

	const DWORD position = fvf & 0x00E; // D3DFVF_POSITION_MASK
	if (position != D3DFVF_XYZ && position != D3DFVF_XYZRHW)
		return layout; // unsupported position format

	int offset       = 0;
	layout.posOffset = 0;
	layout.xyzrhw    = (position == D3DFVF_XYZRHW);
	offset          += layout.xyzrhw ? 16 : 12;

	if (fvf & D3DFVF_NORMAL)
	{
		layout.normalOffset = offset;
		offset             += 12;
	}
	if (fvf & D3DFVF_PSIZE)
		offset += 4;
	if (fvf & D3DFVF_DIFFUSE)
	{
		layout.colorOffset = offset;
		offset            += 4;
	}
	if (fvf & D3DFVF_SPECULAR)
		offset += 4;

	const int texCount = static_cast<int>((fvf >> 8) & 0xF); // D3DFVF_TEXCOUNT
	layout.uvCount     = (texCount > 2) ? 2 : texCount;      // engine uses at most 2
	for (int i = 0; i < layout.uvCount; ++i)
	{
		layout.uvOffset[i] = offset + i * 8;
	}
	offset       += texCount * 8; // all sets are 2-float in this engine

	layout.stride = offset;
	layout.valid  = true;
	return layout;
}

// --- Enum mappings -------------------------------------------------------------

/// D3DBLEND -> GL blend factor. Unknown values fall back to GL_ONE.
inline gl::Enum BlendFactor(DWORD d3dBlend)
{
	switch (d3dBlend)
	{
		case D3DBLEND_ZERO:         return gl::ZERO;
		case D3DBLEND_ONE:          return gl::ONE;
		case D3DBLEND_SRCCOLOR:     return gl::SRC_COLOR;
		case D3DBLEND_INVSRCCOLOR:  return gl::ONE_MINUS_SRC_COLOR;
		case D3DBLEND_SRCALPHA:     return gl::SRC_ALPHA;
		case D3DBLEND_INVSRCALPHA:  return gl::ONE_MINUS_SRC_ALPHA;
		case D3DBLEND_DESTALPHA:    return gl::DST_ALPHA;
		case D3DBLEND_INVDESTALPHA: return gl::ONE_MINUS_DST_ALPHA;
		case D3DBLEND_DESTCOLOR:    return gl::DST_COLOR;
		case D3DBLEND_INVDESTCOLOR: return gl::ONE_MINUS_DST_COLOR;
		case D3DBLEND_SRCALPHASAT:  return gl::SRC_ALPHA_SATURATE;
		default:                    return gl::ONE;
	}
}

/// D3DCMP -> GL depth/stencil comparison function.
inline gl::Enum CompareFunc(DWORD d3dCmp)
{
	switch (d3dCmp)
	{
		case D3DCMP_NEVER:        return gl::NEVER;
		case D3DCMP_LESS:         return gl::LESS;
		case D3DCMP_EQUAL:        return gl::EQUAL;
		case D3DCMP_LESSEQUAL:    return gl::LEQUAL;
		case D3DCMP_GREATER:      return gl::GREATER;
		case D3DCMP_NOTEQUAL:     return gl::NOTEQUAL;
		case D3DCMP_GREATEREQUAL: return gl::GEQUAL;
		case D3DCMP_ALWAYS:       return gl::ALWAYS;
		default:                  return gl::LEQUAL;
	}
}

/// D3DPRIMITIVETYPE -> GL primitive mode.
inline gl::Enum PrimitiveMode(D3DPRIMITIVETYPE type)
{
	switch (type)
	{
		case D3DPT_POINTLIST:     return gl::POINTS;
		case D3DPT_LINELIST:      return gl::LINES;
		case D3DPT_LINESTRIP:     return gl::LINE_STRIP;
		case D3DPT_TRIANGLELIST:  return gl::TRIANGLES;
		case D3DPT_TRIANGLESTRIP: return gl::TRIANGLE_STRIP;
		case D3DPT_TRIANGLEFAN:   return gl::TRIANGLE_FAN;
		default:                  return gl::TRIANGLES;
	}
}

/// D3D primitive count -> element (vertex/index) count.
inline UINT PrimitiveElementCount(D3DPRIMITIVETYPE type, UINT primitiveCount)
{
	switch (type)
	{
		case D3DPT_POINTLIST:     return primitiveCount;
		case D3DPT_LINELIST:      return primitiveCount * 2;
		case D3DPT_LINESTRIP:     return primitiveCount + 1;
		case D3DPT_TRIANGLELIST:  return primitiveCount * 3;
		case D3DPT_TRIANGLESTRIP: return primitiveCount + 2;
		case D3DPT_TRIANGLEFAN:   return primitiveCount + 2;
		default:                  return primitiveCount * 3;
	}
}

/// D3DTEXTUREADDRESS -> GL wrap mode.
inline gl::Enum WrapMode(DWORD d3dAddress)
{
	switch (d3dAddress)
	{
		case D3DTADDRESS_WRAP:   return gl::REPEAT;
		case D3DTADDRESS_MIRROR: return gl::MIRRORED_REPEAT;
		case D3DTADDRESS_CLAMP:  return gl::CLAMP_TO_EDGE;
		case D3DTADDRESS_BORDER: return gl::CLAMP_TO_BORDER;
		default:                 return gl::REPEAT;
	}
}

/// (D3D min filter, D3D mip filter, texture has mips) -> GL minification filter.
inline gl::Enum MinFilter(DWORD d3dMin, DWORD d3dMip, bool bHasMips)
{
	const bool linear = (d3dMin != D3DTEXF_POINT && d3dMin != D3DTEXF_NONE);
	if (!bHasMips || d3dMip == D3DTEXF_NONE)
		return linear ? gl::LINEAR : gl::NEAREST;

	const bool mipLinear = (d3dMip != D3DTEXF_POINT);
	if (linear)
		return mipLinear ? gl::LINEAR_MIPMAP_LINEAR : gl::LINEAR_MIPMAP_NEAREST;
	return mipLinear ? gl::NEAREST_MIPMAP_LINEAR : gl::NEAREST_MIPMAP_NEAREST;
}

inline gl::Enum MagFilter(DWORD d3dMag)
{
	return (d3dMag == D3DTEXF_POINT || d3dMag == D3DTEXF_NONE) ? gl::NEAREST : gl::LINEAR;
}

/// The subset of texture formats the engine loads (see N3Texture).
struct TexUploadFormat
{
	gl::Enum internalFormat = 0;
	gl::Enum format         = 0; // 0 with compressed == glCompressedTexImage2D path
	gl::Enum type           = 0;
	bool compressed         = false;
	bool valid              = false;
};

inline TexUploadFormat TranslateTexFormat(D3DFORMAT d3dFormat)
{
	TexUploadFormat out;
	out.valid = true;
	switch (d3dFormat)
	{
		case D3DFMT_DXT1:
			out.internalFormat = gl::COMPRESSED_RGBA_S3TC_DXT1;
			out.compressed     = true;
			break;
		case D3DFMT_DXT2: // premultiplied variants share the S3TC block layout
		case D3DFMT_DXT3:
			out.internalFormat = gl::COMPRESSED_RGBA_S3TC_DXT3;
			out.compressed     = true;
			break;
		case D3DFMT_DXT4:
		case D3DFMT_DXT5:
			out.internalFormat = gl::COMPRESSED_RGBA_S3TC_DXT5;
			out.compressed     = true;
			break;
		case D3DFMT_A8R8G8B8:
		case D3DFMT_X8R8G8B8:
			out.internalFormat = gl::RGBA8;
			out.format         = gl::BGRA;
			out.type           = gl::UNSIGNED_BYTE;
			break;
		case D3DFMT_A1R5G5B5:
			out.internalFormat = gl::RGB5_A1;
			out.format         = gl::BGRA;
			out.type           = gl::UNSIGNED_SHORT_1_5_5_5_REV;
			break;
		case D3DFMT_A4R4G4B4:
			out.internalFormat = gl::RGBA4;
			out.format         = gl::BGRA;
			out.type           = gl::UNSIGNED_SHORT_4_4_4_4_REV;
			break;
		case D3DFMT_R8G8B8:
			out.internalFormat = gl::RGB8;
			out.format         = gl::BGR;
			out.type           = gl::UNSIGNED_BYTE;
			break;
		default:
			out.valid = false;
			break;
	}
	return out;
}

} // namespace gltr

#endif // CLIENT_WARFARE_GLTRANSLATE_H
