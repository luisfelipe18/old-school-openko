#ifndef N3BASE_RHI_RHIDEVICE_H
#define N3BASE_RHI_RHIDEVICE_H

#pragma once

// Render Hardware Interface (docs/PORT_POSIX_PLAN.md, phase 5).
//
// The interface deliberately mirrors the IDirect3DDevice9 subset the engine
// uses (fixed-function states, FVF geometry, UP draws), so migrating a call
// site is a textual substitution:
//
//     CN3Base::s_lpD3DDev->SetRenderState(...)   [D3D9, Windows-only]
//  -> CN3Base::RHIDevice()->SetRenderState(...)  [any backend, any platform]
//
// Backends: RHIDeviceD3D9 (Windows; forwards to the real device),
// RHIDeviceNull (headless/POSIX until the GL/SDL_GPU backends land - stores
// state so Get* round-trips, counts draws, and lets asset loading run
// without a GPU).

#include "../My_3DStruct.h"
#include "RHIBuffers.h"
#include "RHITextures.h"

// Offscreen render target (docs/ASSET_EXPLORER_PLAN.md, M1). An opaque
// backend-owned color(+depth) surface the engine can render into so a tool can
// then sample the result into its own UI (ImGui::Image). Optional capability:
// backends that can't render offscreen return nullptr from CreateRenderTarget
// and the Begin/End calls are no-ops, so callers must null-check the handle.
struct RHIRenderTargetDesc
{
	UINT width  = 0;
	UINT height = 0;
	bool depth  = true; // attach a depth buffer (needed for 3D previews)
};

struct IRHIRenderTarget
{
	virtual ~IRHIRenderTarget() = default;

	virtual UINT Width() const  = 0;
	virtual UINT Height() const = 0;

	// Backend-native handle to the color texture, suitable for the UI layer:
	// the GL texture name (as void*/uintptr) for the GL backend, an
	// SDL_GPUTexture* for the SDL_GPU backend. nullptr if unavailable.
	virtual void* ColorHandle() const = 0;
};

struct IRHIDevice
{
	virtual ~IRHIDevice() = default;

	// --- Frame ---------------------------------------------------------------
	virtual HRESULT Clear(DWORD flags, D3DCOLOR color, float z, DWORD stencil)             = 0;
	virtual HRESULT BeginScene()                                                           = 0;
	virtual HRESULT EndScene()                                                             = 0;
	virtual HRESULT Present()                                                              = 0;

	// --- Fixed-function state --------------------------------------------------
	virtual HRESULT SetRenderState(DWORD state, DWORD value)                               = 0;
	virtual HRESULT GetRenderState(DWORD state, DWORD* pValue)                             = 0;
	virtual HRESULT SetTextureStageState(DWORD stage, DWORD type, DWORD value)             = 0;
	virtual HRESULT GetTextureStageState(DWORD stage, DWORD type, DWORD* pValue)           = 0;
	virtual HRESULT SetSamplerState(DWORD sampler, DWORD type, DWORD value)                = 0;
	virtual HRESULT GetSamplerState(DWORD sampler, DWORD type, DWORD* pValue)              = 0;
	virtual HRESULT SetTransform(DWORD state, const _D3DMATRIX* pMatrix)                   = 0;
	virtual HRESULT GetTransform(DWORD state, _D3DMATRIX* pMatrix)                         = 0;
	virtual HRESULT SetMaterial(const _D3DMATERIAL9* pMaterial)                            = 0;
	virtual HRESULT SetLight(DWORD index, const _D3DLIGHT9* pLight)                        = 0;
	virtual HRESULT GetLight(DWORD index, _D3DLIGHT9* pLight)                              = 0;
	virtual HRESULT LightEnable(DWORD index, BOOL bEnable)                                 = 0;
	virtual HRESULT GetLightEnable(DWORD index, BOOL* pbEnabled)                           = 0;
	virtual HRESULT SetViewport(const D3DVIEWPORT9* pViewport)                             = 0;
	virtual HRESULT GetViewport(D3DVIEWPORT9* pViewport)                                   = 0;
	virtual HRESULT ValidateDevice(DWORD* pNumPasses)                                      = 0;
	virtual HRESULT SetScissorRect(const RECT* pRect)                                      = 0;

	// --- Textures & geometry ----------------------------------------------------
	virtual HRESULT SetTexture(DWORD stage, IRHITexture* pTexture)                         = 0;
	virtual HRESULT SetFVF(DWORD fvf)                                                      = 0;
	virtual HRESULT GetFVF(DWORD* pFvf)                                                    = 0;

	// Textures (T6.2). Same semantics as IDirect3DDevice9::CreateTexture minus
	// the shared-handle parameter.
	virtual HRESULT CreateTexture(UINT width, UINT height, UINT levels, DWORD usage,
		D3DFORMAT format, D3DPOOL pool, IRHITexture** ppTexture)                           = 0;

	// Geometry buffers (T6.1). Same semantics as the D3D9 Create* calls,
	// minus the shared-handle parameter.
	virtual HRESULT CreateVertexBuffer(
		UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, IRHIVertexBuffer** ppBuffer)     = 0;
	virtual HRESULT CreateIndexBuffer(
		UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, IRHIIndexBuffer** ppBuffer) = 0;

	virtual HRESULT SetStreamSource(
		UINT streamNumber, IRHIVertexBuffer* pBuffer, UINT offsetInBytes, UINT stride)      = 0;
	virtual HRESULT SetIndices(IRHIIndexBuffer* pBuffer)                                    = 0;

	virtual HRESULT DrawPrimitive(
		D3DPRIMITIVETYPE primitiveType, UINT startVertex, UINT primitiveCount)             = 0;
	virtual HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE primitiveType, INT baseVertexIndex,
		UINT minVertexIndex, UINT numVertices, UINT startIndex, UINT primitiveCount)        = 0;
	virtual HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE primitiveType, UINT primitiveCount,
		const void* pVertexData, UINT vertexStride)                                         = 0;
	virtual HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE primitiveType, UINT minVertexIndex,
		UINT numVertices, UINT primitiveCount, const void* pIndexData, D3DFORMAT indexFormat,
		const void* pVertexData, UINT vertexStride)                                         = 0;

	// --- Offscreen render targets (optional capability, M1) --------------------
	// Default implementations make this a no-op on backends that don't support
	// it (D3D9/SDL_GPU/Null today), so only backends that override it - and the
	// tools that opt in - are affected. Create returns an owned handle the
	// caller must delete; Begin/End bracket engine draws that should land in the
	// target instead of the swapchain, and restore the previous target on End.
	virtual IRHIRenderTarget* CreateRenderTarget(const RHIRenderTargetDesc& /*desc*/)
	{
		return nullptr;
	}
	virtual void BeginRenderTarget(IRHIRenderTarget* /*pTarget*/) {}
	virtual void EndRenderTarget() {}

	// --- Pixel-coordinate convention ------------------------------------------
	// D3D9 maps a pre-transformed (XYZRHW) vertex at integer coordinate N to the
	// CORNER between pixels N-1 and N, so the engine subtracts 0.5 from screen-
	// space UI/text vertices to land POINT-sampled texels on pixel centres (see
	// N3UIImage / DFont). GL, SDL_GPU and the Null backend already map N to the
	// pixel centre, so that -0.5 shifts everything ~1px and misaligns quad edges
	// (a faint seam plus a slightly-offset HUD). Only the D3D9 backend needs it.
	virtual bool NeedsHalfPixelOffset() const { return false; }

	// --- Distance fog ---------------------------------------------------------
	// The camera drives EXP2 distance fog (N3Camera::Apply), and the sky's
	// horizon glow (CN3Sky) is drawn as an untextured band meant to blend with
	// that fog at the far plane. Only the D3D9 backend implements EXP2 fog; the
	// GL / SDL_GPU / Null backends do not, so on them the band has nothing to
	// blend into and reads as a hard grey stripe across the horizon. Backends
	// that actually render the camera's distance fog return true so the glow is
	// only drawn where it belongs.
	virtual bool SupportsDistanceFog() const { return false; }
};

#ifndef D3D_OK
#define D3D_OK       ((HRESULT) 0)
#endif
#ifndef RHI_E_FAIL
#define RHI_E_FAIL   ((HRESULT) 0x80004005L) // matches E_FAIL
#endif

#endif // N3BASE_RHI_RHIDEVICE_H
