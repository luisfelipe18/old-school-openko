#include "StdAfx.h"
#include "RHIDeviceSDLGPU.h"
#include "SDLGPUTranslate.h"
#include "shaders/ShaderBlobsSDLGPU.h"

#include <N3Base/N3Base.h>

#include <SDL3/SDL.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
// D3DCOLOR (0xAARRGGBB) -> float RGBA.
void ColorToFloats(D3DCOLOR color, float out[4])
{
	out[0] = ((color >> 16) & 0xFF) / 255.0f;
	out[1] = ((color >> 8) & 0xFF) / 255.0f;
	out[2] = (color & 0xFF) / 255.0f;
	out[3] = ((color >> 24) & 0xFF) / 255.0f;
}

// Render states like FOGSTART store IEEE float bits in the DWORD.
float FloatBits(DWORD dwValue)
{
	float f = 0.0f;
	std::memcpy(&f, &dwValue, sizeof(f));
	return f;
}

// Bytes one mip level occupies in the Null texture's storage (tightly packed,
// 4x4 blocks for DXT) - mirrors RHITextureNull's own allocation.
uint32_t LevelUploadSize(D3DFORMAT format, UINT width, UINT height, const sgtr::TexUploadFormat& fmt)
{
	if (fmt.expandToBgra8)
		return width * height * 4;
	switch (format)
	{
		case D3DFMT_DXT1:
			return std::max(1u, (width + 3) / 4) * std::max(1u, (height + 3) / 4) * 8;
		case D3DFMT_DXT2:
		case D3DFMT_DXT3:
		case D3DFMT_DXT4:
		case D3DFMT_DXT5:
			return std::max(1u, (width + 3) / 4) * std::max(1u, (height + 3) / 4) * 16;
		default:
			return width * height * 4; // BGRA8 paths
	}
}
} // namespace

// ============================================================================
// RHITextureSDLGPU
// ============================================================================

ULONG RHITextureSDLGPU::Release()
{
	// Recorded draws may still hold this texture until the frame flush, so
	// destruction is deferred to the device's graveyard (drained after the
	// next submit). The engine's single-owner contract is preserved: the
	// object is dead to callers as soon as Release() returns.
	m_pDevice->ParkInGraveyard(this);
	return 0;
}

// ============================================================================
// Device setup / teardown
// ============================================================================

RHIDeviceSDLGPU::RHIDeviceSDLGPU(SDL_Window* pWindow, bool bVSync) : m_pWindow(pWindow)
{
	m_pDevice = SDL_CreateGPUDevice(
		SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, /*debug_mode=*/false, nullptr);
	if (m_pDevice == nullptr)
	{
		spdlog::error("RHIDeviceSDLGPU: SDL_CreateGPUDevice failed: {}", SDL_GetError());
		return;
	}

	if (!SDL_ClaimWindowForGPUDevice(m_pDevice, pWindow))
	{
		spdlog::error("RHIDeviceSDLGPU: SDL_ClaimWindowForGPUDevice failed: {}", SDL_GetError());
		SDL_DestroyGPUDevice(m_pDevice);
		m_pDevice = nullptr;
		return;
	}

	SDL_GPUPresentMode presentMode = SDL_GPU_PRESENTMODE_VSYNC;
	if (!bVSync
		&& SDL_WindowSupportsGPUPresentMode(m_pDevice, pWindow, SDL_GPU_PRESENTMODE_IMMEDIATE))
		presentMode = SDL_GPU_PRESENTMODE_IMMEDIATE;
	SDL_SetGPUSwapchainParameters(
		m_pDevice, pWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, presentMode);

	// --- Shaders (precompiled by shaders/build_shaders.sh) ---
	const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(m_pDevice);
	SDL_GPUShaderCreateInfo vsInfo      = {};
	SDL_GPUShaderCreateInfo fsInfo      = {};
	if (supported & SDL_GPU_SHADERFORMAT_SPIRV)
	{
		vsInfo.code       = shader_blobs::UBER_VERT_SPIRV;
		vsInfo.code_size  = shader_blobs::UBER_VERT_SPIRV_len;
		fsInfo.code       = shader_blobs::UBER_FRAG_SPIRV;
		fsInfo.code_size  = shader_blobs::UBER_FRAG_SPIRV_len;
		vsInfo.format = fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
		vsInfo.entrypoint = fsInfo.entrypoint = "main";
	}
	else if (supported & SDL_GPU_SHADERFORMAT_MSL)
	{
		vsInfo.code      = reinterpret_cast<const Uint8*>(shader_blobs::UBER_VERT_MSL);
		vsInfo.code_size = std::strlen(shader_blobs::UBER_VERT_MSL);
		fsInfo.code      = reinterpret_cast<const Uint8*>(shader_blobs::UBER_FRAG_MSL);
		fsInfo.code_size = std::strlen(shader_blobs::UBER_FRAG_MSL);
		vsInfo.format = fsInfo.format = SDL_GPU_SHADERFORMAT_MSL;
		vsInfo.entrypoint = fsInfo.entrypoint = "main0"; // spirv-cross names it main0
	}
	else
	{
		spdlog::error("RHIDeviceSDLGPU: device supports neither SPIR-V nor MSL shaders");
		SDL_ReleaseWindowFromGPUDevice(m_pDevice, pWindow);
		SDL_DestroyGPUDevice(m_pDevice);
		m_pDevice = nullptr;
		return;
	}

	vsInfo.stage               = SDL_GPU_SHADERSTAGE_VERTEX;
	vsInfo.num_uniform_buffers = 1;
	fsInfo.stage               = SDL_GPU_SHADERSTAGE_FRAGMENT;
	fsInfo.num_samplers        = MAX_STAGES;
	fsInfo.num_uniform_buffers = 1;

	m_pVertexShader   = SDL_CreateGPUShader(m_pDevice, &vsInfo);
	m_pFragmentShader = SDL_CreateGPUShader(m_pDevice, &fsInfo);
	if (m_pVertexShader == nullptr || m_pFragmentShader == nullptr)
	{
		spdlog::error("RHIDeviceSDLGPU: shader creation failed: {}", SDL_GetError());
		return; // destructor cleans up what exists
	}

	// --- Depth format ---
	const SDL_GPUTextureFormat depthCandidates[] = {
		SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
		SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
		SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
	};
	for (SDL_GPUTextureFormat candidate : depthCandidates)
	{
		if (SDL_GPUTextureSupportsFormat(m_pDevice, candidate, SDL_GPU_TEXTURETYPE_2D,
				SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
		{
			m_iDepthFormat = candidate;
			break;
		}
	}
	if (m_iDepthFormat == 0)
	{
		spdlog::error("RHIDeviceSDLGPU: no supported depth format");
		return;
	}

	// --- 1x1 opaque white for unbound stages ---
	{
		SDL_GPUTextureCreateInfo texInfo = {};
		texInfo.type                     = SDL_GPU_TEXTURETYPE_2D;
		texInfo.format                   = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
		texInfo.usage                    = SDL_GPU_TEXTUREUSAGE_SAMPLER;
		texInfo.width                    = 1;
		texInfo.height                   = 1;
		texInfo.layer_count_or_depth     = 1;
		texInfo.num_levels               = 1;
		m_pWhiteTexture                  = SDL_CreateGPUTexture(m_pDevice, &texInfo);

		SDL_GPUTransferBufferCreateInfo tbInfo = {};
		tbInfo.usage                           = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		tbInfo.size                            = 4;
		SDL_GPUTransferBuffer* pTB             = SDL_CreateGPUTransferBuffer(m_pDevice, &tbInfo);

		if (m_pWhiteTexture != nullptr && pTB != nullptr)
		{
			if (void* pMap = SDL_MapGPUTransferBuffer(m_pDevice, pTB, false))
			{
				std::memset(pMap, 0xFF, 4);
				SDL_UnmapGPUTransferBuffer(m_pDevice, pTB);
			}
			SDL_GPUCommandBuffer* pCmd = SDL_AcquireGPUCommandBuffer(m_pDevice);
			SDL_GPUCopyPass* pCopy     = SDL_BeginGPUCopyPass(pCmd);
			SDL_GPUTextureTransferInfo src = {};
			src.transfer_buffer            = pTB;
			SDL_GPUTextureRegion dst       = {};
			dst.texture                    = m_pWhiteTexture;
			dst.w = dst.h = dst.d = 1;
			SDL_UploadToGPUTexture(pCopy, &src, &dst, false);
			SDL_EndGPUCopyPass(pCopy);
			SDL_SubmitGPUCommandBuffer(pCmd);
		}
		if (pTB != nullptr)
			SDL_ReleaseGPUTransferBuffer(m_pDevice, pTB);
	}
	if (m_pWhiteTexture == nullptr)
	{
		spdlog::error("RHIDeviceSDLGPU: fallback texture creation failed: {}", SDL_GetError());
		return;
	}

	// --- Engine caps (same slots CN3Eng fills from the D3D caps) ---
	if (SDL_GPUTextureSupportsFormat(m_pDevice, SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM,
			SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER))
	{
		CN3Base::s_dwTextureCaps |= TEX_CAPS_DXT1 | TEX_CAPS_DXT2 | TEX_CAPS_DXT3 | TEX_CAPS_DXT4
									| TEX_CAPS_DXT5;
	}
	// SDL_GPU has no max-size query; 4096 is the guaranteed floor on every
	// backend it supports and comfortably above the engine's largest asset.
	CN3Base::s_DevCaps.MaxTextureWidth  = 4096;
	CN3Base::s_DevCaps.MaxTextureHeight = 4096;

	// Default viewport: the whole window (what D3D gives a fresh device) -
	// same bootstrap the GL backend does; flows that never call SetViewport
	// (e.g. the test scene) rasterize into a 0x0 viewport otherwise. The
	// recorded viewport is LOGICAL, like every game SetViewport call; the
	// physical framebuffer may be larger on HiDPI displays.
	{
		int w = 0, h = 0;
		SDL_GetWindowSize(pWindow, &w, &h);
		int pixelW = 0, pixelH = 0;
		SDL_GetWindowSizeInPixels(pWindow, &pixelW, &pixelH);
		if (w > 0 && pixelW > 0)
			m_fPixelDensity = static_cast<float>(pixelW) / static_cast<float>(w);

		D3DVIEWPORT9 viewport = {};
		viewport.Width        = static_cast<DWORD>(w);
		viewport.Height       = static_cast<DWORD>(h);
		viewport.MinZ         = 0.0f;
		viewport.MaxZ         = 1.0f;
		SetViewport(&viewport);
	}

	spdlog::info("RHIDeviceSDLGPU: driver '{}', {} shaders, BC textures {}, depth format {}",
		SDL_GetGPUDeviceDriver(m_pDevice),
		(vsInfo.format == SDL_GPU_SHADERFORMAT_SPIRV) ? "SPIR-V" : "MSL",
		(CN3Base::s_dwTextureCaps & TEX_CAPS_DXT1) ? "supported" : "NOT supported",
		m_iDepthFormat);

	m_bValid = true;
}

RHIDeviceSDLGPU::~RHIDeviceSDLGPU()
{
	if (m_pDevice == nullptr)
		return;

	SDL_WaitForGPUIdle(m_pDevice);
	DrainGraveyard();

	for (auto& [key, pPipeline] : m_Pipelines)
		SDL_ReleaseGPUGraphicsPipeline(m_pDevice, pPipeline);
	for (auto& [key, pSampler] : m_Samplers)
		SDL_ReleaseGPUSampler(m_pDevice, pSampler);

	if (m_pStreamBuffer != nullptr)
		SDL_ReleaseGPUBuffer(m_pDevice, m_pStreamBuffer);
	if (m_pStreamTransfer != nullptr)
		SDL_ReleaseGPUTransferBuffer(m_pDevice, m_pStreamTransfer);
	if (m_pOffscreenColor != nullptr)
		SDL_ReleaseGPUTexture(m_pDevice, m_pOffscreenColor);
	if (m_pOffscreenDepth != nullptr)
		SDL_ReleaseGPUTexture(m_pDevice, m_pOffscreenDepth);
	if (m_pWhiteTexture != nullptr)
		SDL_ReleaseGPUTexture(m_pDevice, m_pWhiteTexture);
	if (m_pVertexShader != nullptr)
		SDL_ReleaseGPUShader(m_pDevice, m_pVertexShader);
	if (m_pFragmentShader != nullptr)
		SDL_ReleaseGPUShader(m_pDevice, m_pFragmentShader);

	SDL_ReleaseWindowFromGPUDevice(m_pDevice, m_pWindow);
	SDL_DestroyGPUDevice(m_pDevice);
}

void RHIDeviceSDLGPU::ParkInGraveyard(RHITextureSDLGPU* pTexture)
{
	m_Graveyard.push_back(pTexture);
	// Drop it from the current bindings and any pending-upload list; the GPU
	// texture object stays alive until the frame that recorded it flushed.
	for (auto& pBound : m_apBoundTextures)
	{
		if (pBound == pTexture)
			pBound = nullptr;
	}
	m_FrameTextures.erase(
		std::remove(m_FrameTextures.begin(), m_FrameTextures.end(), pTexture),
		m_FrameTextures.end());
}

void RHIDeviceSDLGPU::DrainGraveyard()
{
	for (RHITextureSDLGPU* pTexture : m_Graveyard)
	{
		if (pTexture->m_pGPUTexture != nullptr)
			SDL_ReleaseGPUTexture(m_pDevice, pTexture->m_pGPUTexture);
		delete pTexture;
	}
	m_Graveyard.clear();
}

// ============================================================================
// Resources
// ============================================================================

HRESULT RHIDeviceSDLGPU::CreateTexture(UINT width, UINT height, UINT levels, DWORD /*usage*/,
	D3DFORMAT format, D3DPOOL /*pool*/, IRHITexture** ppTexture)
{
	if (ppTexture == nullptr || width == 0 || height == 0)
		return RHI_E_FAIL;

	if (levels == 0) // full chain, D3D9 semantics
	{
		levels = 1;
		for (UINT w = width, h = height; w > 1 || h > 1; ++levels)
		{
			w = (w > 1) ? w / 2 : 1;
			h = (h > 1) ? h / 2 : 1;
		}
	}

	*ppTexture = new RHITextureSDLGPU(this, width, height, levels, format);
	return D3D_OK;
}

HRESULT RHIDeviceSDLGPU::SetTexture(DWORD stage, IRHITexture* pTexture)
{
	if (stage < MAX_STAGES)
		m_apBoundTextures[stage] = pTexture;
	return D3D_OK;
}

void RHIDeviceSDLGPU::MarkTextureUsed(RHITextureSDLGPU* pTexture)
{
	if (!pTexture->m_bDirtyGPU)
		return;
	if (std::find(m_FrameTextures.begin(), m_FrameTextures.end(), pTexture)
		== m_FrameTextures.end())
		m_FrameTextures.push_back(pTexture);
}

// ============================================================================
// Recording
// ============================================================================

HRESULT RHIDeviceSDLGPU::Clear(DWORD flags, D3DCOLOR color, float z, DWORD /*stencil*/)
{
	Command cmd;
	cmd.type        = Command::Type::Clear;
	cmd.clear.flags = flags;
	cmd.clear.z     = z;
	ColorToFloats(color, cmd.clear.rgba);
	m_Commands.push_back(cmd);
	return D3D_OK;
}

uint32_t RHIDeviceSDLGPU::ArenaAppend(const void* pData, size_t size)
{
	// 4-byte alignment covers both vertex data and 16-bit indices.
	size_t offset = (m_FrameArena.size() + 3) & ~size_t(3);
	m_FrameArena.resize(offset + size);
	std::memcpy(m_FrameArena.data() + offset, pData, size);
	return static_cast<uint32_t>(offset);
}

void RHIDeviceSDLGPU::SnapshotUniforms(const gltr::FVFLayout& layout, VSUniforms& vs, FSUniforms& fs)
{
	DWORD value = 0;
	std::memset(&vs, 0, sizeof(vs));
	std::memset(&fs, 0, sizeof(fs));

	vs.flags[0] = (layout.normalOffset >= 0) ? 1 : 0;
	vs.flags[1] = (layout.colorOffset >= 0) ? 1 : 0;

	if (layout.xyzrhw)
	{
		D3DVIEWPORT9 vp = {};
		GetViewport(&vp);
		// XYZRHW positions are in LOGICAL units: the shader divides by the
		// logical size while the pass viewport covers the whole physical
		// target, which is exactly the HiDPI upscale (matches
		// RHIDeviceGL::ApplyViewport). The recorded D3D viewport is logical
		// already; the physical target size is divided back by the density.
		vs.viewportSize[0] = m_uTargetW ? (static_cast<float>(m_uTargetW) / m_fPixelDensity)
										: static_cast<float>(vp.Width);
		vs.viewportSize[1] = m_uTargetH ? (static_cast<float>(m_uTargetH) / m_fPixelDensity)
										: static_cast<float>(vp.Height);
		vs.viewportSize[2] = 1.0f;
	}
	else
	{
		_D3DMATRIX world = {}, view = {}, proj = {};
		GetTransform(D3DTS_WORLD, &world);
		GetTransform(D3DTS_VIEW, &view);
		GetTransform(D3DTS_PROJECTION, &proj);

		// __Matrix44 shares the row-major D3D layout; copying it straight into
		// the (column-major) std140 mat4 makes the shader read the transpose,
		// which is exactly what the column-vector math needs (v*WVP == WVP^T*v).
		const __Matrix44& w  = *reinterpret_cast<const __Matrix44*>(&world);
		const __Matrix44& v  = *reinterpret_cast<const __Matrix44*>(&view);
		const __Matrix44& p  = *reinterpret_cast<const __Matrix44*>(&proj);
		const __Matrix44 wv  = w * v;
		const __Matrix44 wvp = wv * p;
		std::memcpy(vs.wvp, &wvp, sizeof(vs.wvp));
		std::memcpy(vs.world, &w, sizeof(vs.world));
		std::memcpy(vs.wv, &wv, sizeof(vs.wv));
	}

	// --- Lighting ---
	GetRenderState(D3DRS_LIGHTING, &value);
	const bool bLighting = (value != 0);
	vs.flags[2]          = bLighting ? 1 : 0;

	if (bLighting && layout.normalOffset >= 0)
	{
		GetRenderState(D3DRS_AMBIENT, &value);
		ColorToFloats(value, vs.globalAmbient);

		std::memcpy(vs.matDiffuse, &m_Material.Diffuse.r, sizeof(vs.matDiffuse));
		std::memcpy(vs.matAmbient, &m_Material.Ambient.r, sizeof(vs.matAmbient));
		std::memcpy(vs.matEmissive, &m_Material.Emissive.r, sizeof(vs.matEmissive));

		int count = 0;
		for (const auto& [index, enabled] : m_LightsEnabled)
		{
			if (!enabled || count >= MAX_LIGHTS)
				continue;
			const auto it = m_Lights.find(index);
			if (it == m_Lights.end())
				continue;

			const _D3DLIGHT9& light = it->second;
			vs.lightPos[count][0]   = light.Position.x;
			vs.lightPos[count][1]   = light.Position.y;
			vs.lightPos[count][2]   = light.Position.z;
			vs.lightPos[count][3]   = light.Range;
			vs.lightDir[count][0]   = light.Direction.x;
			vs.lightDir[count][1]   = light.Direction.y;
			vs.lightDir[count][2]   = light.Direction.z;
			vs.lightDir[count][3]   = static_cast<float>(light.Type);
			std::memcpy(vs.lightDiffuse[count], &light.Diffuse.r, 4 * sizeof(float));
			std::memcpy(vs.lightAmbient[count], &light.Ambient.r, 4 * sizeof(float));
			vs.lightAtt[count][0] = light.Attenuation0;
			vs.lightAtt[count][1] = light.Attenuation1;
			vs.lightAtt[count][2] = light.Attenuation2;
			++count;
		}
		vs.flags[3] = count;
	}

	// --- Texture stage combiners ---
	for (DWORD stage = 0; stage < MAX_STAGES; ++stage)
	{
		GetTextureStageState(stage, D3DTSS_COLOROP, &value);
		fs.stageColor[stage][0] = static_cast<int32_t>(value);
		GetTextureStageState(stage, D3DTSS_COLORARG1, &value);
		fs.stageColor[stage][1] = static_cast<int32_t>(value);
		GetTextureStageState(stage, D3DTSS_COLORARG2, &value);
		fs.stageColor[stage][2] = static_cast<int32_t>(value);
		fs.stageColor[stage][3] = (m_apBoundTextures[stage] != nullptr) ? 1 : 0;
		GetTextureStageState(stage, D3DTSS_ALPHAOP, &value);
		fs.stageAlpha[stage][0] = static_cast<int32_t>(value);
		GetTextureStageState(stage, D3DTSS_ALPHAARG1, &value);
		fs.stageAlpha[stage][1] = static_cast<int32_t>(value);
		GetTextureStageState(stage, D3DTSS_ALPHAARG2, &value);
		fs.stageAlpha[stage][2] = static_cast<int32_t>(value);
	}

	GetRenderState(D3DRS_TEXTUREFACTOR, &value);
	ColorToFloats(value, fs.tfactor);

	// --- Alpha test ---
	GetRenderState(D3DRS_ALPHATESTENABLE, &value);
	if (value)
	{
		DWORD func = D3DCMP_ALWAYS, ref = 0;
		GetRenderState(D3DRS_ALPHAFUNC, &func);
		GetRenderState(D3DRS_ALPHAREF, &ref);
		fs.alphaTest[0] = static_cast<float>(func);
		fs.alphaTest[1] = static_cast<float>(ref & 0xFF) / 255.0f;
	}

	// --- Fog (linear ramp over FOGSTART..FOGEND, like the GL backend) ---
	// The engine only uses the camera's distance fog: requested as EXP2 but
	// always paired with linear FOGSTART/FOGEND (0.75*far .. far). The shader
	// applies the linear ramp, so any active fog mode maps onto it and fades
	// distant terrain into the horizon instead of a hard far-clip edge.
	GetRenderState(D3DRS_FOGENABLE, &value);
	bool bFog = (value != 0);
	if (bFog)
	{
		DWORD tableMode = D3DFOG_NONE, vertexMode = D3DFOG_NONE;
		GetRenderState(D3DRS_FOGTABLEMODE, &tableMode);
		GetRenderState(D3DRS_FOGVERTEXMODE, &vertexMode);
		bFog = (tableMode != D3DFOG_NONE || vertexMode != D3DFOG_NONE);
	}
	fs.fogParams[2] = bFog ? 1.0f : 0.0f;
	if (bFog)
	{
		GetRenderState(D3DRS_FOGCOLOR, &value);
		ColorToFloats(value, fs.fogColor);
		GetRenderState(D3DRS_FOGSTART, &value);
		fs.fogParams[0] = FloatBits(value);
		GetRenderState(D3DRS_FOGEND, &value);
		fs.fogParams[1] = FloatBits(value);
	}
}

bool RHIDeviceSDLGPU::BeginRecordDraw(
	D3DPRIMITIVETYPE primitiveType, const gltr::FVFLayout& layout, RecordedDraw& cmd)
{
	if (!m_bValid || !layout.valid)
		return false;

	DWORD value = 0;

	cmd.key.fvf      = m_dwFVF;
	cmd.key.stride   = static_cast<uint16_t>(layout.stride);
	cmd.key.topology = static_cast<uint8_t>(sgtr::PrimitiveTopology(primitiveType));

	GetRenderState(D3DRS_ALPHABLENDENABLE, &value);
	cmd.key.blendEnable = value ? 1 : 0;
	if (value)
	{
		DWORD src = D3DBLEND_ONE, dst = D3DBLEND_ZERO;
		GetRenderState(D3DRS_SRCBLEND, &src);
		GetRenderState(D3DRS_DESTBLEND, &dst);
		cmd.key.srcBlend = static_cast<uint8_t>(src);
		cmd.key.dstBlend = static_cast<uint8_t>(dst);
	}

	GetRenderState(D3DRS_ZENABLE, &value);
	cmd.key.zTest = value ? 1 : 0;
	if (value)
	{
		DWORD zfunc = D3DCMP_LESSEQUAL;
		GetRenderState(D3DRS_ZFUNC, &zfunc);
		cmd.key.zFunc = static_cast<uint8_t>(zfunc);
	}
	GetRenderState(D3DRS_ZWRITEENABLE, &value);
	cmd.key.zWrite = value ? 1 : 0;

	GetRenderState(D3DRS_CULLMODE, &value);
	cmd.key.cull = static_cast<uint8_t>(value);

	GetRenderState(D3DRS_SCISSORTESTENABLE, &value);
	cmd.bScissor = (value != 0);
	if (cmd.bScissor)
		cmd.scissor = ScissorRect();

	cmd.bFullViewport = layout.xyzrhw;
	GetViewport(&cmd.viewport);

	SnapshotUniforms(layout, cmd.vs, cmd.fs);

	for (DWORD stage = 0; stage < MAX_STAGES; ++stage)
	{
		auto* pTexture = static_cast<RHITextureSDLGPU*>(m_apBoundTextures[stage]);
		if (pTexture != nullptr)
		{
			MarkTextureUsed(pTexture);
			// The SDL texture is created at flush; recorded draws reference
			// the CPU object, resolved to the GPU handle during replay
			// (kept alive until then by the graveyard scheme).
			cmd.apTextures[stage] = pTexture;
		}

		DWORD minF = D3DTEXF_POINT, magF = D3DTEXF_POINT, mipF = D3DTEXF_NONE;
		DWORD addrU = D3DTADDRESS_WRAP, addrV = D3DTADDRESS_WRAP;
		GetSamplerState(stage, D3DSAMP_MINFILTER, &minF);
		GetSamplerState(stage, D3DSAMP_MAGFILTER, &magF);
		GetSamplerState(stage, D3DSAMP_MIPFILTER, &mipF);
		GetSamplerState(stage, D3DSAMP_ADDRESSU, &addrU);
		GetSamplerState(stage, D3DSAMP_ADDRESSV, &addrV);
		const bool bHasMips = (pTexture != nullptr) && (pTexture->GetLevelCount() > 1);
		cmd.auSamplerKeys[stage] = sgtr::SamplerKey(minF, magF, mipF, addrU, addrV, bHasMips);
	}

	return true;
}

void RHIDeviceSDLGPU::RecordFanIndices(RecordedDraw& cmd, UINT primitiveCount)
{
	const std::vector<uint16_t> indices = sgtr::ExpandFanIndices(primitiveCount);
	cmd.uIndexBytesOffset = ArenaAppend(indices.data(), indices.size() * sizeof(uint16_t));
	cmd.uElementCount     = static_cast<uint32_t>(indices.size());
	cmd.bIndexed          = true;
}

HRESULT RHIDeviceSDLGPU::DrawPrimitive(
	D3DPRIMITIVETYPE primitiveType, UINT startVertex, UINT primitiveCount)
{
	const gltr::FVFLayout layout = gltr::ParseFVF(m_dwFVF);
	auto* pVB                    = static_cast<RHIVertexBufferNull*>(m_pBoundVB);
	RecordedDraw cmd;
	if (pVB == nullptr || !BeginRecordDraw(primitiveType, layout, cmd))
		return D3D_OK;

	const UINT vertexCount = gltr::PrimitiveElementCount(primitiveType, primitiveCount);
	const size_t byteOffset =
		static_cast<size_t>(m_nStreamOffset) + static_cast<size_t>(startVertex) * layout.stride;
	const size_t byteSize = static_cast<size_t>(vertexCount) * layout.stride;
	if (byteOffset + byteSize > pVB->StorageBytes().size())
		return D3D_OK;

	cmd.uVertexBytesOffset = ArenaAppend(pVB->StorageBytes().data() + byteOffset, byteSize);
	if (primitiveType == D3DPT_TRIANGLEFAN)
		RecordFanIndices(cmd, primitiveCount);
	else
		cmd.uElementCount = vertexCount;

	Command command;
	command.type = Command::Type::Draw;
	command.draw = cmd;
	m_Commands.push_back(command);
	++m_nDrawCalls;
	return D3D_OK;
}

HRESULT RHIDeviceSDLGPU::DrawIndexedPrimitive(D3DPRIMITIVETYPE primitiveType, INT baseVertexIndex,
	UINT minVertexIndex, UINT numVertices, UINT startIndex, UINT primitiveCount)
{
	const gltr::FVFLayout layout = gltr::ParseFVF(m_dwFVF);
	auto* pVB                    = static_cast<RHIVertexBufferNull*>(m_pBoundVB);
	auto* pIB                    = static_cast<RHIIndexBufferNull*>(m_pBoundIB);
	RecordedDraw cmd;
	if (pVB == nullptr || pIB == nullptr || !BeginRecordDraw(primitiveType, layout, cmd))
		return D3D_OK;

	const UINT indexCount = gltr::PrimitiveElementCount(primitiveType, primitiveCount);

	// Copy the referenced vertex window [base+min, base+min+num) and rebase
	// the indices with vertex_offset (index i maps to window slot i - min).
	const size_t vtxByteOffset = static_cast<size_t>(m_nStreamOffset)
								 + (static_cast<size_t>(baseVertexIndex) + minVertexIndex)
									   * layout.stride;
	const size_t vtxByteSize = static_cast<size_t>(numVertices) * layout.stride;
	const size_t idxByteOffset = static_cast<size_t>(startIndex) * 2;
	const size_t idxByteSize   = static_cast<size_t>(indexCount) * 2;
	if (vtxByteOffset + vtxByteSize > pVB->StorageBytes().size()
		|| idxByteOffset + idxByteSize > pIB->StorageBytes().size())
		return D3D_OK;

	cmd.uVertexBytesOffset =
		ArenaAppend(pVB->StorageBytes().data() + vtxByteOffset, vtxByteSize);
	cmd.iVertexOffset = -static_cast<int32_t>(minVertexIndex);

	if (primitiveType == D3DPT_TRIANGLEFAN)
	{
		// Remap the fan's source indices into a triangle list.
		const uint16_t* pSrc = reinterpret_cast<const uint16_t*>(
			pIB->StorageBytes().data() + idxByteOffset);
		std::vector<uint16_t> expanded;
		expanded.reserve(static_cast<size_t>(primitiveCount) * 3);
		for (UINT i = 0; i < primitiveCount; ++i)
		{
			expanded.push_back(pSrc[0]);
			expanded.push_back(pSrc[i + 1]);
			expanded.push_back(pSrc[i + 2]);
		}
		cmd.uIndexBytesOffset = ArenaAppend(expanded.data(), expanded.size() * sizeof(uint16_t));
		cmd.uElementCount     = static_cast<uint32_t>(expanded.size());
	}
	else
	{
		cmd.uIndexBytesOffset = ArenaAppend(pIB->StorageBytes().data() + idxByteOffset, idxByteSize);
		cmd.uElementCount     = indexCount;
	}
	cmd.bIndexed = true;

	Command command;
	command.type = Command::Type::Draw;
	command.draw = cmd;
	m_Commands.push_back(command);
	++m_nDrawCalls;
	return D3D_OK;
}

HRESULT RHIDeviceSDLGPU::DrawPrimitiveUP(
	D3DPRIMITIVETYPE primitiveType, UINT primitiveCount, const void* pVertexData, UINT vertexStride)
{
	if (pVertexData == nullptr)
		return RHI_E_FAIL;

	const gltr::FVFLayout layout = gltr::ParseFVF(m_dwFVF);
	RecordedDraw cmd;
	if (!BeginRecordDraw(primitiveType, layout, cmd))
		return D3D_OK;

	const UINT vertexCount = gltr::PrimitiveElementCount(primitiveType, primitiveCount);
	cmd.uVertexBytesOffset =
		ArenaAppend(pVertexData, static_cast<size_t>(vertexCount) * vertexStride);
	if (primitiveType == D3DPT_TRIANGLEFAN)
		RecordFanIndices(cmd, primitiveCount);
	else
		cmd.uElementCount = vertexCount;

	Command command;
	command.type = Command::Type::Draw;
	command.draw = cmd;
	m_Commands.push_back(command);
	++m_nDrawCalls;
	return D3D_OK;
}

HRESULT RHIDeviceSDLGPU::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE primitiveType,
	UINT minVertexIndex, UINT numVertices, UINT primitiveCount, const void* pIndexData,
	D3DFORMAT /*indexFormat*/, const void* pVertexData, UINT vertexStride)
{
	if (pIndexData == nullptr || pVertexData == nullptr)
		return RHI_E_FAIL;

	const gltr::FVFLayout layout = gltr::ParseFVF(m_dwFVF);
	RecordedDraw cmd;
	if (!BeginRecordDraw(primitiveType, layout, cmd))
		return D3D_OK;

	const UINT indexCount = gltr::PrimitiveElementCount(primitiveType, primitiveCount);
	cmd.uVertexBytesOffset = ArenaAppend(
		pVertexData, static_cast<size_t>(minVertexIndex + numVertices) * vertexStride);

	if (primitiveType == D3DPT_TRIANGLEFAN)
	{
		const uint16_t* pSrc = static_cast<const uint16_t*>(pIndexData);
		std::vector<uint16_t> expanded;
		expanded.reserve(static_cast<size_t>(primitiveCount) * 3);
		for (UINT i = 0; i < primitiveCount; ++i)
		{
			expanded.push_back(pSrc[0]);
			expanded.push_back(pSrc[i + 1]);
			expanded.push_back(pSrc[i + 2]);
		}
		cmd.uIndexBytesOffset = ArenaAppend(expanded.data(), expanded.size() * sizeof(uint16_t));
		cmd.uElementCount     = static_cast<uint32_t>(expanded.size());
	}
	else
	{
		cmd.uIndexBytesOffset = ArenaAppend(pIndexData, static_cast<size_t>(indexCount) * 2);
		cmd.uElementCount     = indexCount;
	}
	cmd.bIndexed = true;

	Command command;
	command.type = Command::Type::Draw;
	command.draw = cmd;
	m_Commands.push_back(command);
	++m_nDrawCalls;
	return D3D_OK;
}

// ============================================================================
// Flush (Present)
// ============================================================================

bool RHIDeviceSDLGPU::EnsureRenderTargets(uint32_t width, uint32_t height)
{
	if (m_pOffscreenColor != nullptr && m_uTargetW == width && m_uTargetH == height)
		return true;

	if (m_pOffscreenColor != nullptr)
		SDL_ReleaseGPUTexture(m_pDevice, m_pOffscreenColor);
	if (m_pOffscreenDepth != nullptr)
		SDL_ReleaseGPUTexture(m_pDevice, m_pOffscreenDepth);

	SDL_GPUTextureCreateInfo colorInfo = {};
	colorInfo.type                     = SDL_GPU_TEXTURETYPE_2D;
	colorInfo.format                   = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
	colorInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	colorInfo.width                    = width;
	colorInfo.height                   = height;
	colorInfo.layer_count_or_depth     = 1;
	colorInfo.num_levels               = 1;
	m_pOffscreenColor                  = SDL_CreateGPUTexture(m_pDevice, &colorInfo);

	SDL_GPUTextureCreateInfo depthInfo = colorInfo;
	depthInfo.format                   = static_cast<SDL_GPUTextureFormat>(m_iDepthFormat);
	depthInfo.usage                    = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
	m_pOffscreenDepth                  = SDL_CreateGPUTexture(m_pDevice, &depthInfo);

	if (m_pOffscreenColor == nullptr || m_pOffscreenDepth == nullptr)
	{
		spdlog::error("RHIDeviceSDLGPU: render target creation failed: {}", SDL_GetError());
		return false;
	}

	m_uTargetW = width;
	m_uTargetH = height;
	return true;
}

SDL_GPUGraphicsPipeline* RHIDeviceSDLGPU::GetOrCreatePipeline(const PipelineKey& key)
{
	const auto it = m_Pipelines.find(key);
	if (it != m_Pipelines.end())
		return it->second;

	const gltr::FVFLayout layout = gltr::ParseFVF(key.fvf);

	// Every shader input must be fed (Vulkan rule). Attributes the FVF lacks
	// alias offset 0 - always inside the vertex - and the shader ignores them
	// via the hasNormal/hasColor flags.
	SDL_GPUVertexBufferDescription bufferDesc = {};
	bufferDesc.slot                           = 0;
	bufferDesc.pitch                          = key.stride;
	bufferDesc.input_rate                     = SDL_GPU_VERTEXINPUTRATE_VERTEX;

	SDL_GPUVertexAttribute attributes[5] = {};
	attributes[0].location               = 0;
	attributes[0].format                 = layout.xyzrhw ? SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4
														 : SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
	attributes[0].offset                 = static_cast<Uint32>(layout.posOffset);
	attributes[1].location               = 1;
	attributes[1].format                 = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
	attributes[1].offset = static_cast<Uint32>(std::max(layout.normalOffset, 0));
	attributes[2].location               = 2;
	attributes[2].format                 = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
	attributes[2].offset = static_cast<Uint32>(std::max(layout.colorOffset, 0));
	attributes[3].location               = 3;
	attributes[3].format                 = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
	attributes[3].offset = static_cast<Uint32>(std::max(layout.uvOffset[0], 0));
	attributes[4].location               = 4;
	attributes[4].format                 = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
	attributes[4].offset =
		static_cast<Uint32>(std::max(layout.uvOffset[1] >= 0 ? layout.uvOffset[1]
															 : layout.uvOffset[0],
			0));

	SDL_GPUColorTargetBlendState blend = {};
	blend.enable_blend                 = (key.blendEnable != 0);
	blend.color_blend_op               = SDL_GPU_BLENDOP_ADD;
	blend.alpha_blend_op               = SDL_GPU_BLENDOP_ADD;
	blend.src_color_blendfactor        = sgtr::BlendFactor(key.srcBlend);
	blend.dst_color_blendfactor        = sgtr::BlendFactor(key.dstBlend);
	blend.src_alpha_blendfactor        = sgtr::BlendFactor(key.srcBlend);
	blend.dst_alpha_blendfactor        = sgtr::BlendFactor(key.dstBlend);

	SDL_GPUColorTargetDescription colorTarget = {};
	colorTarget.format                        = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
	colorTarget.blend_state                   = blend;

	SDL_GPUGraphicsPipelineCreateInfo info      = {};
	info.vertex_shader                          = m_pVertexShader;
	info.fragment_shader                        = m_pFragmentShader;
	info.vertex_input_state.vertex_buffer_descriptions = &bufferDesc;
	info.vertex_input_state.num_vertex_buffers  = 1;
	info.vertex_input_state.vertex_attributes   = attributes;
	info.vertex_input_state.num_vertex_attributes = 5;
	info.primitive_type = static_cast<SDL_GPUPrimitiveType>(key.topology);

	// Both D3D and SDL_GPU classify winding with a top-left origin, so the
	// D3D convention maps directly: front = clockwise, CULL_CCW culls the
	// back face and CULL_CW the front one.
	info.rasterizer_state.fill_mode         = SDL_GPU_FILLMODE_FILL;
	info.rasterizer_state.front_face        = SDL_GPU_FRONTFACE_CLOCKWISE;
	info.rasterizer_state.cull_mode         = (key.cull == D3DCULL_NONE) ? SDL_GPU_CULLMODE_NONE
											  : (key.cull == D3DCULL_CCW) ? SDL_GPU_CULLMODE_BACK
																		  : SDL_GPU_CULLMODE_FRONT;
	info.rasterizer_state.enable_depth_clip = true;

	info.depth_stencil_state.enable_depth_test  = (key.zTest != 0);
	info.depth_stencil_state.enable_depth_write = (key.zWrite != 0);
	info.depth_stencil_state.compare_op =
		(key.zTest != 0) ? sgtr::CompareOp(key.zFunc) : SDL_GPU_COMPAREOP_ALWAYS;

	info.target_info.color_target_descriptions = &colorTarget;
	info.target_info.num_color_targets         = 1;
	info.target_info.depth_stencil_format = static_cast<SDL_GPUTextureFormat>(m_iDepthFormat);
	info.target_info.has_depth_stencil_target = true;

	SDL_GPUGraphicsPipeline* pPipeline = SDL_CreateGPUGraphicsPipeline(m_pDevice, &info);
	if (pPipeline == nullptr)
		spdlog::error("RHIDeviceSDLGPU: pipeline creation failed: {}", SDL_GetError());

	m_Pipelines.emplace(key, pPipeline);
	return pPipeline;
}

SDL_GPUSampler* RHIDeviceSDLGPU::GetOrCreateSampler(uint32_t samplerKey)
{
	const auto it = m_Samplers.find(samplerKey);
	if (it != m_Samplers.end())
		return it->second;

	const DWORD minF  = samplerKey & 0xF;
	const DWORD magF  = (samplerKey >> 4) & 0xF;
	const DWORD mipF  = (samplerKey >> 8) & 0xF;
	const DWORD addrU = (samplerKey >> 12) & 0xF;
	const DWORD addrV = (samplerKey >> 16) & 0xF;
	const bool bMips  = (samplerKey & (1u << 20)) != 0;

	const sgtr::MipSampling mip = sgtr::MipMode(mipF, bMips);

	SDL_GPUSamplerCreateInfo info = {};
	info.min_filter               = sgtr::Filter(minF);
	info.mag_filter               = sgtr::Filter(magF);
	info.mipmap_mode              = mip.mode;
	info.address_mode_u           = sgtr::AddressMode(addrU);
	info.address_mode_v           = sgtr::AddressMode(addrV);
	info.address_mode_w           = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	info.max_lod                  = mip.maxLod;

	SDL_GPUSampler* pSampler = SDL_CreateGPUSampler(m_pDevice, &info);
	m_Samplers.emplace(samplerKey, pSampler);
	return pSampler;
}

void RHIDeviceSDLGPU::UploadFrameResources(void* pCopyCmdBufRaw)
{
	auto* pCmd = static_cast<SDL_GPUCommandBuffer*>(pCopyCmdBufRaw);

	// --- Stream buffer (all vertex/index bytes of the frame) ---
	const uint32_t arenaSize = static_cast<uint32_t>(m_FrameArena.size());

	// --- Texture bytes ---
	uint32_t textureBytes = 0;
	for (RHITextureSDLGPU* pTexture : m_FrameTextures)
	{
		const sgtr::TexUploadFormat fmt = sgtr::TranslateTexFormat(pTexture->m_eFormat);
		if (!fmt.valid)
			continue;
		for (UINT level = 0; level < pTexture->GetLevelCount(); ++level)
		{
			D3DSURFACE_DESC desc = {};
			pTexture->GetLevelDesc(level, &desc);
			textureBytes += LevelUploadSize(pTexture->m_eFormat, desc.Width, desc.Height, fmt) + 16;
		}
	}

	const uint32_t transferNeeded = arenaSize + textureBytes + 64;
	if (m_pStreamTransfer == nullptr || m_uStreamTransferSize < transferNeeded)
	{
		if (m_pStreamTransfer != nullptr)
			SDL_ReleaseGPUTransferBuffer(m_pDevice, m_pStreamTransfer);
		m_uStreamTransferSize                  = std::max(transferNeeded, 1u << 20);
		SDL_GPUTransferBufferCreateInfo tbInfo = {};
		tbInfo.usage                           = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		tbInfo.size                            = m_uStreamTransferSize;
		m_pStreamTransfer = SDL_CreateGPUTransferBuffer(m_pDevice, &tbInfo);
	}
	if (arenaSize > 0 && (m_pStreamBuffer == nullptr || m_uStreamBufferSize < arenaSize))
	{
		if (m_pStreamBuffer != nullptr)
			SDL_ReleaseGPUBuffer(m_pDevice, m_pStreamBuffer);
		m_uStreamBufferSize              = std::max(arenaSize, 1u << 20);
		SDL_GPUBufferCreateInfo bufInfo  = {};
		bufInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_INDEX;
		bufInfo.size                     = m_uStreamBufferSize;
		m_pStreamBuffer                  = SDL_CreateGPUBuffer(m_pDevice, &bufInfo);
	}
	if (m_pStreamTransfer == nullptr || (arenaSize > 0 && m_pStreamBuffer == nullptr))
	{
		spdlog::error("RHIDeviceSDLGPU: stream buffer creation failed: {}", SDL_GetError());
		return;
	}

	uint8_t* pMap = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(m_pDevice, m_pStreamTransfer, true));
	if (pMap == nullptr)
	{
		spdlog::error("RHIDeviceSDLGPU: SDL_MapGPUTransferBuffer failed: {}", SDL_GetError());
		return;
	}

	uint32_t cursor = 0;
	if (arenaSize > 0)
	{
		std::memcpy(pMap, m_FrameArena.data(), arenaSize);
		cursor = (arenaSize + 15) & ~15u;
	}

	// Stage every dirty texture's levels behind the arena bytes.
	struct PendingTexUpload
	{
		RHITextureSDLGPU* pTexture;
		UINT level;
		uint32_t offset;
		UINT width, height;
	};
	std::vector<PendingTexUpload> texUploads;

	for (RHITextureSDLGPU* pTexture : m_FrameTextures)
	{
		const sgtr::TexUploadFormat fmt = sgtr::TranslateTexFormat(pTexture->m_eFormat);
		if (!fmt.valid)
		{
			spdlog::warn(
				"RHIDeviceSDLGPU: unsupported texture format {}", uint32_t(pTexture->m_eFormat));
			pTexture->m_bDirtyGPU = false;
			continue;
		}

		if (pTexture->m_pGPUTexture == nullptr)
		{
			SDL_GPUTextureCreateInfo texInfo = {};
			texInfo.type                     = SDL_GPU_TEXTURETYPE_2D;
			texInfo.format                   = fmt.format;
			texInfo.usage                    = SDL_GPU_TEXTUREUSAGE_SAMPLER;
			D3DSURFACE_DESC desc             = {};
			pTexture->GetLevelDesc(0, &desc);
			texInfo.width                = desc.Width;
			texInfo.height               = desc.Height;
			texInfo.layer_count_or_depth = 1;
			texInfo.num_levels           = pTexture->GetLevelCount();
			pTexture->m_pGPUTexture      = SDL_CreateGPUTexture(m_pDevice, &texInfo);
			if (pTexture->m_pGPUTexture == nullptr)
			{
				spdlog::error("RHIDeviceSDLGPU: texture creation failed: {}", SDL_GetError());
				pTexture->m_bDirtyGPU = false;
				continue;
			}
		}

		for (UINT level = 0; level < pTexture->GetLevelCount(); ++level)
		{
			// Friend access to the Null texture's CPU levels: cheaper and
			// side-effect-free compared to LockRect (whose Unlock would
			// re-mark the texture dirty).
			const auto& levelData = pTexture->m_Levels[level];
			const uint32_t size =
				LevelUploadSize(pTexture->m_eFormat, levelData.width, levelData.height, fmt);

			if (fmt.expandToBgra8)
			{
				const std::vector<uint8_t> expanded = sgtr::ExpandToBgra8(pTexture->m_eFormat,
					levelData.storage.data(), levelData.width, levelData.height, levelData.pitch);
				std::memcpy(pMap + cursor, expanded.data(), expanded.size());
			}
			else
			{
				const size_t copySize =
					std::min(static_cast<size_t>(size), levelData.storage.size());
				std::memcpy(pMap + cursor, levelData.storage.data(), copySize);
				if (fmt.fillAlpha)
				{
					for (uint32_t i = 3; i < size; i += 4)
						pMap[cursor + i] = 0xFF;
				}
			}

			texUploads.push_back({ pTexture, level, cursor, levelData.width, levelData.height });
			cursor = (cursor + size + 15) & ~15u;
		}

		pTexture->m_bDirtyGPU = false;
	}

	SDL_UnmapGPUTransferBuffer(m_pDevice, m_pStreamTransfer);

	SDL_GPUCopyPass* pCopy = SDL_BeginGPUCopyPass(pCmd);

	if (arenaSize > 0)
	{
		SDL_GPUTransferBufferLocation src = {};
		src.transfer_buffer               = m_pStreamTransfer;
		SDL_GPUBufferRegion dst           = {};
		dst.buffer                        = m_pStreamBuffer;
		dst.size                          = arenaSize;
		SDL_UploadToGPUBuffer(pCopy, &src, &dst, true);
	}

	for (const PendingTexUpload& upload : texUploads)
	{
		SDL_GPUTextureTransferInfo src = {};
		src.transfer_buffer            = m_pStreamTransfer;
		src.offset                     = upload.offset;
		SDL_GPUTextureRegion dst       = {};
		dst.texture                    = upload.pTexture->m_pGPUTexture;
		dst.mip_level                  = upload.level;
		dst.w                          = upload.width;
		dst.h                          = upload.height;
		dst.d                          = 1;
		SDL_UploadToGPUTexture(pCopy, &src, &dst, false);
	}

	SDL_EndGPUCopyPass(pCopy);
}

HRESULT RHIDeviceSDLGPU::Present()
{
	if (!m_bValid)
		return RHIDeviceNull::Present();

	SDL_GPUCommandBuffer* pCmd = SDL_AcquireGPUCommandBuffer(m_pDevice);
	if (pCmd == nullptr)
	{
		m_Commands.clear();
		m_FrameArena.clear();
		m_FrameTextures.clear();
		return RHIDeviceNull::Present();
	}

	SDL_GPUTexture* pSwapchain = nullptr;
	Uint32 swapW = 0, swapH = 0;
	if (!SDL_WaitAndAcquireGPUSwapchainTexture(pCmd, m_pWindow, &pSwapchain, &swapW, &swapH)
		|| pSwapchain == nullptr || !EnsureRenderTargets(swapW, swapH))
	{
		// Minimized/unavailable: drop the frame but keep the machine moving.
		SDL_SubmitGPUCommandBuffer(pCmd);
		m_Commands.clear();
		m_FrameArena.clear();
		m_FrameTextures.clear();
		DrainGraveyard();
		return RHIDeviceNull::Present();
	}

	UploadFrameResources(pCmd);

	// HiDPI: refresh the density each frame (window resizes, display moves).
	{
		int wPoints = 0, hPoints = 0;
		SDL_GetWindowSize(m_pWindow, &wPoints, &hPoints);
		if (wPoints > 0 && swapW > 0)
			m_fPixelDensity = static_cast<float>(swapW) / static_cast<float>(wPoints);
	}

	// --- Replay ---
	SDL_GPURenderPass* pPass = nullptr;

	// Diagnostics: counts that discriminate "textures never uploaded" from
	// "uniforms wrong" style failures without spamming - logged for the
	// first frames and then once every ~10 seconds. nUntexturedTexDraws is
	// the white-object signature: the stage-0 combiner asks for TEXTURE
	// (D3DTA_TEXTURE) but no texture was bound at record time, so the
	// shader substitutes opaque white.
	int nDraws = 0, nTexturedDraws = 0, nFallbackBinds = 0, nUntexturedTexDraws = 0;

	// First pass of the frame defaults to a full clear so an uninitialized
	// offscreen target never leaks through (the engine clears every frame
	// anyway; this only matters for clear-less diagnostic frames).
	bool bPendingColorClear = true;
	SDL_FColor pendingColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	bool bPendingDepthClear = true;
	float pendingDepth      = 1.0f;

	const auto BeginPass = [&]()
	{
		SDL_GPUColorTargetInfo color = {};
		color.texture                = m_pOffscreenColor;
		color.load_op     = bPendingColorClear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
		color.store_op    = SDL_GPU_STOREOP_STORE;
		color.clear_color = pendingColor;

		SDL_GPUDepthStencilTargetInfo depth = {};
		depth.texture                       = m_pOffscreenDepth;
		depth.load_op     = bPendingDepthClear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
		depth.store_op    = SDL_GPU_STOREOP_STORE;
		depth.clear_depth = pendingDepth;
		depth.stencil_load_op  = SDL_GPU_LOADOP_CLEAR;
		depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

		pPass              = SDL_BeginGPURenderPass(pCmd, &color, 1, &depth);
		bPendingColorClear = false;
		bPendingDepthClear = false;
	};

	for (const Command& command : m_Commands)
	{
		if (command.type == Command::Type::Clear)
		{
			if (pPass != nullptr)
			{
				SDL_EndGPURenderPass(pPass);
				pPass = nullptr;
			}
			if (command.clear.flags & D3DCLEAR_TARGET)
			{
				bPendingColorClear = true;
				pendingColor       = { command.clear.rgba[0], command.clear.rgba[1],
										  command.clear.rgba[2], command.clear.rgba[3] };
			}
			if (command.clear.flags & D3DCLEAR_ZBUFFER)
			{
				bPendingDepthClear = true;
				pendingDepth       = command.clear.z;
			}
			continue;
		}

		const RecordedDraw& draw = command.draw;

		SDL_GPUGraphicsPipeline* pPipeline = GetOrCreatePipeline(draw.key);
		if (pPipeline == nullptr)
			continue;

		if (pPass == nullptr)
			BeginPass();

		SDL_BindGPUGraphicsPipeline(pPass, pPipeline);

		SDL_GPUViewport viewport = {};
		if (draw.bFullViewport)
		{
			viewport.w         = static_cast<float>(m_uTargetW);
			viewport.h         = static_cast<float>(m_uTargetH);
			viewport.max_depth = 1.0f;
		}
		else
		{
			// Recorded viewports are logical; the target is physical (HiDPI).
			viewport.x         = static_cast<float>(draw.viewport.X) * m_fPixelDensity;
			viewport.y         = static_cast<float>(draw.viewport.Y) * m_fPixelDensity;
			viewport.w         = static_cast<float>(draw.viewport.Width) * m_fPixelDensity;
			viewport.h         = static_cast<float>(draw.viewport.Height) * m_fPixelDensity;
			viewport.min_depth = draw.viewport.MinZ;
			viewport.max_depth = draw.viewport.MaxZ;
		}
		SDL_SetGPUViewport(pPass, &viewport);

		SDL_Rect scissor = {};
		if (draw.bScissor)
		{
			// Scissor rects come from the engine in logical units too.
			const auto iLeft   = static_cast<int>(std::lround(draw.scissor.left * m_fPixelDensity));
			const auto iTop    = static_cast<int>(std::lround(draw.scissor.top * m_fPixelDensity));
			const auto iRight  = static_cast<int>(std::lround(draw.scissor.right * m_fPixelDensity));
			const auto iBottom = static_cast<int>(std::lround(draw.scissor.bottom * m_fPixelDensity));
			scissor.x = iLeft;
			scissor.y = iTop;
			scissor.w = iRight - iLeft;
			scissor.h = iBottom - iTop;
		}
		else
		{
			scissor.w = static_cast<int>(m_uTargetW);
			scissor.h = static_cast<int>(m_uTargetH);
		}
		SDL_SetGPUScissor(pPass, &scissor);

		SDL_GPUBufferBinding vertexBinding = {};
		vertexBinding.buffer               = m_pStreamBuffer;
		vertexBinding.offset               = draw.uVertexBytesOffset;
		SDL_BindGPUVertexBuffers(pPass, 0, &vertexBinding, 1);

		++nDraws;
		{
			const int32_t op   = draw.fs.stageColor[0][0];
			const int32_t arg1 = draw.fs.stageColor[0][1];
			const int32_t arg2 = draw.fs.stageColor[0][2];
			const bool bWantsTexture =
				(op != 1) && (arg1 == 2 /*D3DTA_TEXTURE*/ || arg2 == 2);
			if (bWantsTexture && draw.fs.stageColor[0][3] == 0)
				++nUntexturedTexDraws;
		}
		SDL_GPUTextureSamplerBinding samplerBindings[MAX_STAGES] = {};
		for (int stage = 0; stage < MAX_STAGES; ++stage)
		{
			RHITextureSDLGPU* pTexture = draw.apTextures[stage];
			if (pTexture != nullptr)
			{
				++nTexturedDraws;
				if (pTexture->m_pGPUTexture == nullptr)
					++nFallbackBinds; // recorded a texture that never reached the GPU
			}
			samplerBindings[stage].texture =
				(pTexture != nullptr && pTexture->m_pGPUTexture != nullptr)
					? pTexture->m_pGPUTexture
					: m_pWhiteTexture;
			samplerBindings[stage].sampler = GetOrCreateSampler(draw.auSamplerKeys[stage]);
		}
		SDL_BindGPUFragmentSamplers(pPass, 0, samplerBindings, MAX_STAGES);

		SDL_PushGPUVertexUniformData(pCmd, 0, &draw.vs, sizeof(draw.vs));
		SDL_PushGPUFragmentUniformData(pCmd, 0, &draw.fs, sizeof(draw.fs));

		if (draw.bIndexed)
		{
			SDL_GPUBufferBinding indexBinding = {};
			indexBinding.buffer               = m_pStreamBuffer;
			indexBinding.offset               = draw.uIndexBytesOffset;
			SDL_BindGPUIndexBuffer(pPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
			SDL_DrawGPUIndexedPrimitives(
				pPass, draw.uElementCount, 1, 0, draw.iVertexOffset, 0);
		}
		else
		{
			SDL_DrawGPUPrimitives(pPass, draw.uElementCount, 1, 0, 0);
		}
	}

	if (pPass != nullptr)
	{
		SDL_EndGPURenderPass(pPass);
	}
	else
	{
		// Clear-only (or empty) frame: run one pass so the clears land.
		BeginPass();
		SDL_EndGPURenderPass(pPass);
	}
	pPass = nullptr;

	// Offscreen -> swapchain (same size, no filtering ambiguity).
	SDL_GPUBlitInfo blit          = {};
	blit.source.texture           = m_pOffscreenColor;
	blit.source.w                 = m_uTargetW;
	blit.source.h                 = m_uTargetH;
	blit.destination.texture      = pSwapchain;
	blit.destination.w            = swapW;
	blit.destination.h            = swapH;
	blit.load_op                  = SDL_GPU_LOADOP_DONT_CARE;
	blit.filter                   = SDL_GPU_FILTER_NEAREST;
	SDL_BlitGPUTexture(pCmd, &blit);

	SDL_SubmitGPUCommandBuffer(pCmd);

	// Sparse frame diagnostics: the first 3 drawing frames and one line
	// every 600 frames afterwards.
	if (nDraws > 0)
	{
		static int s_nLoggedFrames = 0;
		++s_nLoggedFrames;
		if (s_nLoggedFrames <= 3 || (s_nLoggedFrames % 300) == 0)
		{
			spdlog::info("RHIDeviceSDLGPU: frame {}: {} draws, {} texture binds "
						 "({} without GPU texture, {} wanted a texture but had none), "
						 "{} arena bytes, {} textures uploaded",
				s_nLoggedFrames, nDraws, nTexturedDraws, nFallbackBinds, nUntexturedTexDraws,
				m_FrameArena.size(), m_FrameTextures.size());
		}
		if (nFallbackBinds > 0)
		{
			static bool s_bWarned = false;
			if (!s_bWarned)
			{
				s_bWarned = true;
				spdlog::warn("RHIDeviceSDLGPU: {} draw(s) referenced textures that never got a "
							 "GPU object - they sample opaque white",
					nFallbackBinds);
			}
		}
	}

	m_Commands.clear();
	m_FrameArena.clear();
	m_FrameTextures.clear();
	DrainGraveyard();

	return RHIDeviceNull::Present();
}

// ============================================================================
// Test hooks
// ============================================================================

bool RHIDeviceSDLGPU::DownloadOffscreen(std::vector<uint8_t>& bgraOut, uint32_t& w, uint32_t& h)
{
	if (!m_bValid || m_pOffscreenColor == nullptr)
		return false;

	w = m_uTargetW;
	h = m_uTargetH;
	const uint32_t byteSize = w * h * 4;

	SDL_GPUTransferBufferCreateInfo tbInfo = {};
	tbInfo.usage                           = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	tbInfo.size                            = byteSize;
	SDL_GPUTransferBuffer* pTB             = SDL_CreateGPUTransferBuffer(m_pDevice, &tbInfo);
	if (pTB == nullptr)
		return false;

	SDL_GPUCommandBuffer* pCmd = SDL_AcquireGPUCommandBuffer(m_pDevice);
	SDL_GPUCopyPass* pCopy     = SDL_BeginGPUCopyPass(pCmd);
	SDL_GPUTextureRegion src   = {};
	src.texture                = m_pOffscreenColor;
	src.w                      = w;
	src.h                      = h;
	src.d                      = 1;
	SDL_GPUTextureTransferInfo dst = {};
	dst.transfer_buffer            = pTB;
	SDL_DownloadFromGPUTexture(pCopy, &src, &dst);
	SDL_EndGPUCopyPass(pCopy);

	SDL_GPUFence* pFence = SDL_SubmitGPUCommandBufferAndAcquireFence(pCmd);
	if (pFence != nullptr)
	{
		SDL_WaitForGPUFences(m_pDevice, true, &pFence, 1);
		SDL_ReleaseGPUFence(m_pDevice, pFence);
	}

	bool bOK = false;
	if (void* pMap = SDL_MapGPUTransferBuffer(m_pDevice, pTB, false))
	{
		bgraOut.assign(static_cast<uint8_t*>(pMap), static_cast<uint8_t*>(pMap) + byteSize);
		SDL_UnmapGPUTransferBuffer(m_pDevice, pTB);
		bOK = true;
	}
	SDL_ReleaseGPUTransferBuffer(m_pDevice, pTB);
	return bOK;
}

bool RHIDeviceSDLGPU::ReadCenterPixel(uint8_t rgbaOut[4])
{
	std::vector<uint8_t> bgra;
	uint32_t w = 0, h = 0;
	if (!DownloadOffscreen(bgra, w, h) || w == 0 || h == 0)
		return false;

	const size_t offset = (static_cast<size_t>(h / 2) * w + w / 2) * 4;
	rgbaOut[0] = bgra[offset + 2];
	rgbaOut[1] = bgra[offset + 1];
	rgbaOut[2] = bgra[offset + 0];
	rgbaOut[3] = bgra[offset + 3];
	return true;
}

bool RHIDeviceSDLGPU::DumpFramePPM(const char* szPath)
{
	std::vector<uint8_t> bgra;
	uint32_t w = 0, h = 0;
	if (!DownloadOffscreen(bgra, w, h))
		return false;

	FILE* pFile = fopen(szPath, "wb");
	if (pFile == nullptr)
		return false;

	fprintf(pFile, "P6\n%u %u\n255\n", w, h);
	std::vector<uint8_t> row(static_cast<size_t>(w) * 3);
	for (uint32_t y = 0; y < h; ++y) // rows are already top-down
	{
		const uint8_t* pSrc = bgra.data() + static_cast<size_t>(y) * w * 4;
		for (uint32_t x = 0; x < w; ++x)
		{
			row[x * 3 + 0] = pSrc[x * 4 + 2];
			row[x * 3 + 1] = pSrc[x * 4 + 1];
			row[x * 3 + 2] = pSrc[x * 4 + 0];
		}
		fwrite(row.data(), 1, row.size(), pFile);
	}
	fclose(pFile);
	return true;
}
