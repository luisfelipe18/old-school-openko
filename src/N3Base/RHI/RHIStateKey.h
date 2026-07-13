#ifndef N3BASE_RHI_RHISTATEKEY_H
#define N3BASE_RHI_RHISTATEKEY_H

#pragma once

// Pipeline-state key (docs/PORT_POSIX_PLAN.md, phases 5/6b).
//
// The GL bring-up backend applies render states directly, but the SDL_GPU
// (Metal/Vulkan) backend needs precompiled pipeline objects keyed by the
// state combination in effect at draw time. This key captures exactly the
// state that determines a pipeline, so backends can cache pipelines by its
// hash from day one without touching game code.

#include <cstddef>
#include <cstdint>

struct RHIStateKey
{
	uint32_t fvf              = 0; // vertex layout
	uint8_t primitiveType     = 0; // D3DPRIMITIVETYPE
	uint8_t srcBlend          = 0; // D3DBLEND when blending is on
	uint8_t destBlend         = 0;
	uint8_t cullMode          = 0; // D3DCULL
	uint8_t zFunc             = 0; // D3DCMPFUNC
	uint8_t alphaFunc         = 0; // D3DCMPFUNC when alpha test is on
	uint8_t fogMode           = 0; // D3DFOGMODE
	uint8_t colorOp0          = 0; // stage-0/1 texture combiners (D3DTEXTUREOP)
	uint8_t colorOp1          = 0;
	uint8_t alphaOp0          = 0;
	uint8_t alphaOp1          = 0;

	bool alphaBlendEnable     = false;
	bool alphaTestEnable      = false;
	bool zEnable              = false;
	bool zWriteEnable         = false;
	bool fogEnable            = false;
	bool lightingEnable       = false;
	bool specularEnable       = false;

	bool operator==(const RHIStateKey& other) const
	{
		return fvf == other.fvf && primitiveType == other.primitiveType
			   && srcBlend == other.srcBlend && destBlend == other.destBlend
			   && cullMode == other.cullMode && zFunc == other.zFunc
			   && alphaFunc == other.alphaFunc && fogMode == other.fogMode
			   && colorOp0 == other.colorOp0 && colorOp1 == other.colorOp1
			   && alphaOp0 == other.alphaOp0 && alphaOp1 == other.alphaOp1
			   && alphaBlendEnable == other.alphaBlendEnable
			   && alphaTestEnable == other.alphaTestEnable && zEnable == other.zEnable
			   && zWriteEnable == other.zWriteEnable && fogEnable == other.fogEnable
			   && lightingEnable == other.lightingEnable
			   && specularEnable == other.specularEnable;
	}

	bool operator!=(const RHIStateKey& other) const
	{
		return !(*this == other);
	}

	// FNV-1a over the logical fields (not the raw bytes: padding must not
	// leak into the hash).
	size_t Hash() const
	{
		uint64_t hash        = 0xcbf29ce484222325ULL;
		const auto mix       = [&hash](uint64_t value) {
			hash ^= value;
			hash *= 0x100000001b3ULL;
		};

		mix(fvf);
		mix(primitiveType);
		mix(srcBlend);
		mix(destBlend);
		mix(cullMode);
		mix(zFunc);
		mix(alphaFunc);
		mix(fogMode);
		mix(colorOp0);
		mix(colorOp1);
		mix(alphaOp0);
		mix(alphaOp1);
		mix((alphaBlendEnable ? 1 : 0) | (alphaTestEnable ? 2 : 0) | (zEnable ? 4 : 0)
			| (zWriteEnable ? 8 : 0) | (fogEnable ? 16 : 0) | (lightingEnable ? 32 : 0)
			| (specularEnable ? 64 : 0));

		return static_cast<size_t>(hash);
	}
};

struct RHIStateKeyHasher
{
	size_t operator()(const RHIStateKey& key) const
	{
		return key.Hash();
	}
};

#endif // N3BASE_RHI_RHISTATEKEY_H
