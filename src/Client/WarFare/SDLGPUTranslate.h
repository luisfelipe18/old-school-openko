#ifndef CLIENT_WARFARE_SDLGPUTRANSLATE_H
#define CLIENT_WARFARE_SDLGPUTRANSLATE_H

#pragma once

// Pure Direct3D 9 -> SDL_GPU translation helpers for the RHI SDL_GPU backend
// (docs/PORT_POSIX_PLAN.md, T6b.2). Everything here is stateless and makes no
// GPU calls, so it is unit-tested headlessly (tests/WarFare). The FVF layout
// parsing is shared with the GL backend (gltr::ParseFVF in GLTranslate.h).

#include <N3Base/My_3DStruct.h>

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <vector>

namespace sgtr
{

// --- Enum mappings -------------------------------------------------------------

/// D3DBLEND -> SDL_GPU blend factor. Unknown values fall back to ONE.
inline SDL_GPUBlendFactor BlendFactor(DWORD d3dBlend)
{
	switch (d3dBlend)
	{
		case D3DBLEND_ZERO:         return SDL_GPU_BLENDFACTOR_ZERO;
		case D3DBLEND_ONE:          return SDL_GPU_BLENDFACTOR_ONE;
		case D3DBLEND_SRCCOLOR:     return SDL_GPU_BLENDFACTOR_SRC_COLOR;
		case D3DBLEND_INVSRCCOLOR:  return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
		case D3DBLEND_SRCALPHA:     return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
		case D3DBLEND_INVSRCALPHA:  return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
		case D3DBLEND_DESTALPHA:    return SDL_GPU_BLENDFACTOR_DST_ALPHA;
		case D3DBLEND_INVDESTALPHA: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
		case D3DBLEND_DESTCOLOR:    return SDL_GPU_BLENDFACTOR_DST_COLOR;
		case D3DBLEND_INVDESTCOLOR: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
		case D3DBLEND_SRCALPHASAT:  return SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE;
		default:                    return SDL_GPU_BLENDFACTOR_ONE;
	}
}

/// D3DCMP -> SDL_GPU comparison op.
inline SDL_GPUCompareOp CompareOp(DWORD d3dCmp)
{
	switch (d3dCmp)
	{
		case D3DCMP_NEVER:        return SDL_GPU_COMPAREOP_NEVER;
		case D3DCMP_LESS:         return SDL_GPU_COMPAREOP_LESS;
		case D3DCMP_EQUAL:        return SDL_GPU_COMPAREOP_EQUAL;
		case D3DCMP_LESSEQUAL:    return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
		case D3DCMP_GREATER:      return SDL_GPU_COMPAREOP_GREATER;
		case D3DCMP_NOTEQUAL:     return SDL_GPU_COMPAREOP_NOT_EQUAL;
		case D3DCMP_GREATEREQUAL: return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
		case D3DCMP_ALWAYS:       return SDL_GPU_COMPAREOP_ALWAYS;
		default:                  return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
	}
}

/// D3DPRIMITIVETYPE -> SDL_GPU topology. SDL_GPU has no triangle fans or line
/// strips on every backend; fans are expanded to indexed triangle lists by
/// ExpandFanIndices below (the engine's UI draws quads as 4-vertex fans).
inline SDL_GPUPrimitiveType PrimitiveTopology(D3DPRIMITIVETYPE type)
{
	switch (type)
	{
		case D3DPT_POINTLIST:     return SDL_GPU_PRIMITIVETYPE_POINTLIST;
		case D3DPT_LINELIST:      return SDL_GPU_PRIMITIVETYPE_LINELIST;
		case D3DPT_LINESTRIP:     return SDL_GPU_PRIMITIVETYPE_LINESTRIP;
		case D3DPT_TRIANGLELIST:  return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		case D3DPT_TRIANGLESTRIP: return SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
		case D3DPT_TRIANGLEFAN:   return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST; // via ExpandFanIndices
		default:                  return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	}
}

/// Builds the index list that turns a triangle fan of `primitiveCount`
/// triangles into a triangle list: (0,1,2), (0,2,3), ...
inline std::vector<uint16_t> ExpandFanIndices(UINT primitiveCount)
{
	std::vector<uint16_t> indices;
	indices.reserve(static_cast<size_t>(primitiveCount) * 3);
	for (UINT i = 0; i < primitiveCount; ++i)
	{
		indices.push_back(0);
		indices.push_back(static_cast<uint16_t>(i + 1));
		indices.push_back(static_cast<uint16_t>(i + 2));
	}
	return indices;
}

/// D3DTEXTUREADDRESS -> SDL_GPU address mode. SDL_GPU has no border mode;
/// the engine only sets BORDER with color 0 on stages it then disables, so
/// CLAMP_TO_EDGE is the closest behaviour.
inline SDL_GPUSamplerAddressMode AddressMode(DWORD d3dAddress)
{
	switch (d3dAddress)
	{
		case D3DTADDRESS_WRAP:   return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
		case D3DTADDRESS_MIRROR: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
		case D3DTADDRESS_CLAMP:  return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		case D3DTADDRESS_BORDER: return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		default:                 return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	}
}

inline SDL_GPUFilter Filter(DWORD d3dFilter)
{
	return (d3dFilter == D3DTEXF_POINT || d3dFilter == D3DTEXF_NONE) ? SDL_GPU_FILTER_NEAREST
																	 : SDL_GPU_FILTER_LINEAR;
}

/// (D3D mip filter, texture has mips) -> SDL_GPU mipmap mode + whether to
/// restrict sampling to the base level (D3DTEXF_NONE semantics).
struct MipSampling
{
	SDL_GPUSamplerMipmapMode mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	float maxLod                  = 0.0f; // 0 = base level only
};

inline MipSampling MipMode(DWORD d3dMip, bool bHasMips)
{
	MipSampling out;
	if (!bHasMips || d3dMip == D3DTEXF_NONE)
		return out; // nearest + maxLod 0 pins the base level
	out.mode   = (d3dMip == D3DTEXF_POINT) ? SDL_GPU_SAMPLERMIPMAPMODE_NEAREST
										   : SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
	out.maxLod = 1000.0f;
	return out;
}

/// Packs the sampler-affecting D3D state into a cache key.
inline uint32_t SamplerKey(DWORD minF, DWORD magF, DWORD mipF, DWORD addrU, DWORD addrV, bool bHasMips)
{
	return (minF & 0xF) | ((magF & 0xF) << 4) | ((mipF & 0xF) << 8) | ((addrU & 0xF) << 12)
		   | ((addrV & 0xF) << 16) | (bHasMips ? (1u << 20) : 0u);
}

// --- Texture upload formats ------------------------------------------------------

/// How a D3DFORMAT reaches the GPU: either passed through directly, or
/// CPU-expanded to B8G8R8A8 first (SDL_GPU has no 16-bit packed BGRA or
/// 24-bit formats).
struct TexUploadFormat
{
	SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
	bool expandToBgra8          = false; // A1R5G5B5 / A4R4G4B4 / R8G8B8
	bool fillAlpha              = false; // X8R8G8B8: force A = 0xFF on upload
	bool valid                  = false;
};

/// The subset of texture formats the engine loads (mirrors
/// gltr::TranslateTexFormat, see N3Texture).
inline TexUploadFormat TranslateTexFormat(D3DFORMAT d3dFormat)
{
	TexUploadFormat out;
	out.valid = true;
	switch (d3dFormat)
	{
		case D3DFMT_DXT1:
			out.format = SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
			break;
		case D3DFMT_DXT2: // premultiplied variants share the S3TC block layout
		case D3DFMT_DXT3:
			out.format = SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM;
			break;
		case D3DFMT_DXT4:
		case D3DFMT_DXT5:
			out.format = SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
			break;
		case D3DFMT_A8R8G8B8:
			out.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
			break;
		case D3DFMT_X8R8G8B8:
			out.format    = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
			out.fillAlpha = true;
			break;
		case D3DFMT_A1R5G5B5:
		case D3DFMT_A4R4G4B4:
		case D3DFMT_R8G8B8:
			out.format        = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
			out.expandToBgra8 = true;
			break;
		default:
			out.valid = false;
			break;
	}
	return out;
}

/// CPU expansion of the 16-bit packed / 24-bit D3D formats to B8G8R8A8.
/// `src` is one mip level in its native layout; returns width*height*4 bytes.
inline std::vector<uint8_t> ExpandToBgra8(
	D3DFORMAT d3dFormat, const uint8_t* src, UINT width, UINT height, INT srcPitch)
{
	std::vector<uint8_t> out(static_cast<size_t>(width) * height * 4);

	for (UINT y = 0; y < height; ++y)
	{
		const uint8_t* row = src + static_cast<size_t>(y) * srcPitch;
		uint8_t* dst       = out.data() + static_cast<size_t>(y) * width * 4;

		for (UINT x = 0; x < width; ++x, dst += 4)
		{
			switch (d3dFormat)
			{
				case D3DFMT_A1R5G5B5:
				{
					uint16_t v = static_cast<uint16_t>(row[x * 2] | (row[x * 2 + 1] << 8));
					uint8_t r5 = (v >> 10) & 0x1F, g5 = (v >> 5) & 0x1F, b5 = v & 0x1F;
					dst[0] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
					dst[1] = static_cast<uint8_t>((g5 << 3) | (g5 >> 2));
					dst[2] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
					dst[3] = (v & 0x8000) ? 0xFF : 0x00;
					break;
				}
				case D3DFMT_A4R4G4B4:
				{
					uint16_t v = static_cast<uint16_t>(row[x * 2] | (row[x * 2 + 1] << 8));
					uint8_t a4 = (v >> 12) & 0xF, r4 = (v >> 8) & 0xF, g4 = (v >> 4) & 0xF,
							b4 = v & 0xF;
					dst[0] = static_cast<uint8_t>(b4 * 17);
					dst[1] = static_cast<uint8_t>(g4 * 17);
					dst[2] = static_cast<uint8_t>(r4 * 17);
					dst[3] = static_cast<uint8_t>(a4 * 17);
					break;
				}
				case D3DFMT_R8G8B8:
					dst[0] = row[x * 3 + 0]; // B
					dst[1] = row[x * 3 + 1]; // G
					dst[2] = row[x * 3 + 2]; // R
					dst[3] = 0xFF;
					break;
				default:
					break;
			}
		}
	}

	return out;
}

} // namespace sgtr

#endif // CLIENT_WARFARE_SDLGPUTRANSLATE_H
