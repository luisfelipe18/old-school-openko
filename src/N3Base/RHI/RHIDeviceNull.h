#ifndef N3BASE_RHI_RHIDEVICENULL_H
#define N3BASE_RHI_RHIDEVICENULL_H

#pragma once

// Headless IRHIDevice: stores state so Get* calls round-trip and counts
// draws, but renders nothing. Used by the POSIX build until the GL/SDL_GPU
// backends land (phases 6/6b), by the --smoke CI runs, and by unit tests.

#include "RHIDevice.h"

#include <map>

class RHIDeviceNull : public IRHIDevice
{
public:
	HRESULT Clear(DWORD flags, D3DCOLOR color, float z, DWORD stencil) override;
	HRESULT BeginScene() override;
	HRESULT EndScene() override;
	HRESULT Present() override;

	HRESULT SetRenderState(DWORD state, DWORD value) override;
	HRESULT GetRenderState(DWORD state, DWORD* pValue) override;
	HRESULT SetTextureStageState(DWORD stage, DWORD type, DWORD value) override;
	HRESULT GetTextureStageState(DWORD stage, DWORD type, DWORD* pValue) override;
	HRESULT SetSamplerState(DWORD sampler, DWORD type, DWORD value) override;
	HRESULT GetSamplerState(DWORD sampler, DWORD type, DWORD* pValue) override;
	HRESULT SetTransform(DWORD state, const _D3DMATRIX* pMatrix) override;
	HRESULT GetTransform(DWORD state, _D3DMATRIX* pMatrix) override;
	HRESULT SetMaterial(const _D3DMATERIAL9* pMaterial) override;
	HRESULT SetLight(DWORD index, const _D3DLIGHT9* pLight) override;
	HRESULT GetLight(DWORD index, _D3DLIGHT9* pLight) override;
	HRESULT LightEnable(DWORD index, BOOL bEnable) override;
	HRESULT GetLightEnable(DWORD index, BOOL* pbEnabled) override;
	HRESULT SetViewport(const D3DVIEWPORT9* pViewport) override;
	HRESULT GetViewport(D3DVIEWPORT9* pViewport) override;

	HRESULT SetTexture(DWORD stage, LPDIRECT3DBASETEXTURE9 pTexture) override;
	HRESULT SetFVF(DWORD fvf) override;
	HRESULT GetFVF(DWORD* pFvf) override;

	HRESULT SetStreamSource(
		UINT streamNumber, LPDIRECT3DVERTEXBUFFER9 pBuffer, UINT offsetInBytes, UINT stride) override;
	HRESULT SetIndices(LPDIRECT3DINDEXBUFFER9 pBuffer) override;

	HRESULT DrawPrimitive(
		D3DPRIMITIVETYPE primitiveType, UINT startVertex, UINT primitiveCount) override;
	HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE primitiveType, INT baseVertexIndex,
		UINT minVertexIndex, UINT numVertices, UINT startIndex, UINT primitiveCount) override;
	HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE primitiveType, UINT primitiveCount,
		const void* pVertexData, UINT vertexStride) override;
	HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE primitiveType, UINT minVertexIndex,
		UINT numVertices, UINT primitiveCount, const void* pIndexData, D3DFORMAT indexFormat,
		const void* pVertexData, UINT vertexStride) override;

	// Diagnostics for tests and smoke runs.
	int DrawCallCount() const
	{
		return m_nDrawCalls;
	}
	int PresentCount() const
	{
		return m_nPresents;
	}

protected:
	std::map<DWORD, DWORD> m_RenderStates;
	std::map<uint64_t, DWORD> m_StageStates;   // (stage << 32) | type
	std::map<uint64_t, DWORD> m_SamplerStates; // (sampler << 32) | type
	std::map<DWORD, __Matrix44> m_Transforms;
	std::map<DWORD, _D3DLIGHT9> m_Lights;
	std::map<DWORD, BOOL> m_LightsEnabled;
	D3DVIEWPORT9 m_Viewport = {};
	DWORD m_dwFVF           = 0;
	int m_nDrawCalls        = 0;
	int m_nPresents         = 0;
};

#endif // N3BASE_RHI_RHIDEVICENULL_H
