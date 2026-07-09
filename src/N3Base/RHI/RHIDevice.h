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

	// --- Textures & geometry ----------------------------------------------------
	// Textures stay opaque D3D pointers until the texture RHI lands (phase 6).
	virtual HRESULT SetTexture(DWORD stage, LPDIRECT3DBASETEXTURE9 pTexture)               = 0;
	virtual HRESULT SetFVF(DWORD fvf)                                                      = 0;
	virtual HRESULT GetFVF(DWORD* pFvf)                                                    = 0;

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
};

#ifndef D3D_OK
#define D3D_OK       ((HRESULT) 0)
#endif
#ifndef RHI_E_FAIL
#define RHI_E_FAIL   ((HRESULT) 0x80004005L) // matches E_FAIL
#endif

#endif // N3BASE_RHI_RHIDEVICE_H
