#ifndef CLIENT_WARFARE_RHIDEVICESDLGPU_H
#define CLIENT_WARFARE_RHIDEVICESDLGPU_H

#pragma once

// SDL_GPU RHI backend (docs/PORT_POSIX_PLAN.md, F6b): Metal on macOS, Vulkan
// on Linux, replacing the deprecated-on-macOS OpenGL backend as the long-term
// path. Same contract as RHIDeviceGL: derives from RHIDeviceNull for state
// storage and CPU-side resources, and renders the engine's D3D9
// fixed-function usage through the precompiled uber-shader
// (shaders/uber_sdlgpu.*, see shaders/build_shaders.sh).
//
// Architecture - record then replay: D3D9 lets the engine upload data and
// draw at any point inside BeginScene/EndScene, but SDL_GPU forbids copy
// passes inside a render pass. So draws are RECORDED during the frame (state
// snapshot + vertex/index bytes copied into a frame arena; the engine draws
// almost everything through the *UP paths from system memory anyway), and
// Present() then executes one copy pass with every upload followed by the
// render pass(es) that replay the recorded draws. Mid-frame Clear() becomes
// a render-pass break with the matching load ops. Rendering goes to an
// offscreen color+depth target and is blitted to the swapchain at the end,
// which also gives the test hooks (ReadCenterPixel/DumpFrameToPPM) a
// readable texture.

#include <N3Base/RHI/RHIDeviceNull.h>

#include "GLTranslate.h" // gltr::ParseFVF / PrimitiveElementCount (pure, GL-free)

#include <cstdint>
#include <unordered_map>
#include <vector>

struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUTexture;
struct SDL_GPUSampler;
struct SDL_GPUBuffer;
struct SDL_GPUTransferBuffer;
struct SDL_GPUShader;
struct SDL_GPUGraphicsPipeline;

class RHIDeviceSDLGPU;

/// Texture with CPU storage (from RHITextureNull) plus a lazily-created
/// SDL_GPUTexture that Present() re-uploads whenever the CPU side changed.
/// Release() parks the object in the device's graveyard instead of deleting
/// immediately: recorded draws may still reference it until the frame flush.
class RHITextureSDLGPU final : public RHITextureNull
{
public:
	RHITextureSDLGPU(RHIDeviceSDLGPU* pDevice, UINT width, UINT height, UINT levels, D3DFORMAT format)
		: RHITextureNull(width, height, levels, format), m_pDevice(pDevice)
	{
	}

	HRESULT UnlockRect(UINT level) override
	{
		m_bDirtyGPU = true;
		return RHITextureNull::UnlockRect(level);
	}

	ULONG Release() override; // parks in the device graveyard

	// Backend internals (Present-time upload).
	friend class RHIDeviceSDLGPU;

private:
	~RHITextureSDLGPU() override = default;

	RHIDeviceSDLGPU* m_pDevice = nullptr;
	SDL_GPUTexture* m_pGPUTexture = nullptr;
	bool m_bDirtyGPU = true;
};

class RHIDeviceSDLGPU final : public RHIDeviceNull
{
public:
	RHIDeviceSDLGPU(SDL_Window* pWindow, bool bVSync);
	~RHIDeviceSDLGPU() override;

	bool IsValid() const
	{
		return m_bValid;
	}

	HRESULT Clear(DWORD flags, D3DCOLOR color, float z, DWORD stencil) override;
	HRESULT Present() override;

	HRESULT CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format,
		D3DPOOL pool, IRHITexture** ppTexture) override;

	HRESULT SetTexture(DWORD stage, IRHITexture* pTexture) override;

	HRESULT DrawPrimitive(
		D3DPRIMITIVETYPE primitiveType, UINT startVertex, UINT primitiveCount) override;
	HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE primitiveType, INT baseVertexIndex,
		UINT minVertexIndex, UINT numVertices, UINT startIndex, UINT primitiveCount) override;
	HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE primitiveType, UINT primitiveCount,
		const void* pVertexData, UINT vertexStride) override;
	HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE primitiveType, UINT minVertexIndex,
		UINT numVertices, UINT primitiveCount, const void* pIndexData, D3DFORMAT indexFormat,
		const void* pVertexData, UINT vertexStride) override;

	// Test/diagnostic hooks (same contract as RHIDeviceGL's). They read the
	// offscreen target, so call them after a Present() flushed the frame.
	bool ReadCenterPixel(uint8_t rgbaOut[4]);
	bool DumpFramePPM(const char* szPath);

	// Internal: RHITextureSDLGPU::Release() parks itself here.
	void ParkInGraveyard(RHITextureSDLGPU* pTexture);

private:
	// --- Uniform blocks: byte-for-byte the std140 layouts in uber_sdlgpu.* ---
	static constexpr int MAX_STAGES = 3;
	static constexpr int MAX_LIGHTS = 8;

	struct VSUniforms
	{
		float wvp[16];
		float world[16];
		float wv[16];
		float viewportSize[4]; // x = w, y = h, z = preTransformed
		int32_t flags[4];      // hasNormal, hasColor, lighting, lightCount
		float globalAmbient[4];
		float matDiffuse[4];
		float matAmbient[4];
		float matEmissive[4];
		float lightPos[MAX_LIGHTS][4];
		float lightDir[MAX_LIGHTS][4];
		float lightDiffuse[MAX_LIGHTS][4];
		float lightAmbient[MAX_LIGHTS][4];
		float lightAtt[MAX_LIGHTS][4];
	};
	static_assert(sizeof(VSUniforms) == 928, "must match the std140 block in uber_sdlgpu.vert");

	struct FSUniforms
	{
		int32_t stageColor[MAX_STAGES][4]; // op, arg1, arg2, texBound
		int32_t stageAlpha[MAX_STAGES][4]; // op, arg1, arg2, 0
		float tfactor[4];
		float fogColor[4];
		float fogParams[4]; // start, end, enable
		float alphaTest[4]; // func, ref
	};
	static_assert(sizeof(FSUniforms) == 160, "must match the std140 block in uber_sdlgpu.frag");

	// --- Pipeline cache ---
	struct PipelineKey
	{
		uint32_t fvf         = 0;
		uint16_t stride      = 0;
		uint8_t topology     = 0; // SDL_GPUPrimitiveType
		uint8_t blendEnable  = 0;
		uint8_t srcBlend     = 0; // D3DBLEND
		uint8_t dstBlend     = 0;
		uint8_t zTest        = 0;
		uint8_t zWrite       = 0;
		uint8_t zFunc        = 0; // D3DCMP
		uint8_t cull         = 0; // D3DCULL

		bool operator==(const PipelineKey& o) const
		{
			return fvf == o.fvf && stride == o.stride && topology == o.topology
				   && blendEnable == o.blendEnable && srcBlend == o.srcBlend
				   && dstBlend == o.dstBlend && zTest == o.zTest && zWrite == o.zWrite
				   && zFunc == o.zFunc && cull == o.cull;
		}
	};
	struct PipelineKeyHash
	{
		size_t operator()(const PipelineKey& k) const
		{
			// FNV-1a over the packed fields.
			uint64_t h = 1469598103934665603ull;
			auto mix   = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
			mix(k.fvf);
			mix(k.stride);
			mix((uint64_t(k.topology) << 56) | (uint64_t(k.blendEnable) << 48)
				| (uint64_t(k.srcBlend) << 40) | (uint64_t(k.dstBlend) << 32)
				| (uint64_t(k.zTest) << 24) | (uint64_t(k.zWrite) << 16)
				| (uint64_t(k.zFunc) << 8) | uint64_t(k.cull));
			return static_cast<size_t>(h);
		}
	};

	// --- Recorded commands ---
	struct RecordedDraw
	{
		PipelineKey key;
		VSUniforms vs;
		FSUniforms fs;
		RHITextureSDLGPU* apTextures[MAX_STAGES] = {};
		uint32_t auSamplerKeys[MAX_STAGES]     = {};
		bool bScissor                          = false;
		RECT scissor                           = {};
		D3DVIEWPORT9 viewport                  = {};
		bool bFullViewport                     = false; // XYZRHW path: whole target
		uint32_t uVertexBytesOffset            = 0;     // into the frame arena
		uint32_t uIndexBytesOffset             = 0;
		uint32_t uElementCount                 = 0; // vertices or indices
		int32_t iVertexOffset                  = 0; // vertex_offset for indexed draws
		bool bIndexed                          = false;
	};
	struct RecordedClear
	{
		DWORD flags   = 0;
		float rgba[4] = {};
		float z       = 1.0f;
	};
	struct Command
	{
		enum class Type
		{
			Draw,
			Clear
		} type = Type::Draw;
		RecordedDraw draw;
		RecordedClear clear;
	};

	// --- Recording helpers ---
	void SnapshotUniforms(const gltr::FVFLayout& layout, VSUniforms& vs, FSUniforms& fs);
	bool BeginRecordDraw(D3DPRIMITIVETYPE primitiveType, const gltr::FVFLayout& layout,
		RecordedDraw& cmd);
	uint32_t ArenaAppend(const void* pData, size_t size);
	void RecordFanIndices(RecordedDraw& cmd, UINT primitiveCount);
	void MarkTextureUsed(RHITextureSDLGPU* pTexture);

	// --- Flush (Present) helpers ---
	bool EnsureRenderTargets(uint32_t width, uint32_t height);
	SDL_GPUGraphicsPipeline* GetOrCreatePipeline(const PipelineKey& key);
	SDL_GPUSampler* GetOrCreateSampler(uint32_t samplerKey);
	void UploadFrameResources(void* pCopyCmdBuf); // SDL_GPUCommandBuffer*
	void DrainGraveyard();
	bool DownloadOffscreen(std::vector<uint8_t>& bgraOut, uint32_t& w, uint32_t& h);

	SDL_Window* m_pWindow      = nullptr;
	SDL_GPUDevice* m_pDevice   = nullptr;
	SDL_GPUShader* m_pVertexShader   = nullptr;
	SDL_GPUShader* m_pFragmentShader = nullptr;

	// Offscreen render target (blitted to the swapchain each Present).
	SDL_GPUTexture* m_pOffscreenColor = nullptr;
	SDL_GPUTexture* m_pOffscreenDepth = nullptr;
	uint32_t m_uTargetW = 0, m_uTargetH = 0;
	int m_iDepthFormat = 0; // SDL_GPUTextureFormat

	// HiDPI: framebuffer pixels per logical unit (SDL_WINDOW_HIGH_PIXEL_DENSITY).
	// Engine coordinates (viewport, scissor, XYZRHW) are logical; the replay in
	// Flush scales them to the physical target. Refreshed per frame.
	float m_fPixelDensity = 1.0f;

	// 1x1 opaque white: bound to unbound stages (SDL_GPU requires every
	// declared sampler slot to be bound; the shader ignores them via flags).
	SDL_GPUTexture* m_pWhiteTexture = nullptr;

	// Frame arena: CPU staging for every draw's vertex/index bytes, uploaded
	// in one copy pass at Present.
	std::vector<uint8_t> m_FrameArena;
	SDL_GPUBuffer* m_pStreamBuffer            = nullptr;
	uint32_t m_uStreamBufferSize              = 0;
	SDL_GPUTransferBuffer* m_pStreamTransfer  = nullptr;
	uint32_t m_uStreamTransferSize            = 0;

	std::vector<Command> m_Commands;
	std::vector<RHITextureSDLGPU*> m_FrameTextures; // dirty textures to upload
	std::vector<RHITextureSDLGPU*> m_Graveyard;     // released mid-frame

	std::unordered_map<PipelineKey, SDL_GPUGraphicsPipeline*, PipelineKeyHash> m_Pipelines;
	std::unordered_map<uint32_t, SDL_GPUSampler*> m_Samplers;

	IRHITexture* m_apBoundTextures[MAX_STAGES] = {};

	UINT m_nStreamOffset = 0;
	UINT m_nStreamStride = 0;

	bool m_bValid = false;

public:
	HRESULT SetStreamSource(
		UINT streamNumber, IRHIVertexBuffer* pBuffer, UINT offsetInBytes, UINT stride) override
	{
		m_nStreamOffset = offsetInBytes;
		m_nStreamStride = stride;
		return RHIDeviceNull::SetStreamSource(streamNumber, pBuffer, offsetInBytes, stride);
	}
};

#endif // CLIENT_WARFARE_RHIDEVICESDLGPU_H
