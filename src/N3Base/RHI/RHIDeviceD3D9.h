#ifndef N3BASE_RHI_RHIDEVICED3D9_H
#define N3BASE_RHI_RHIDEVICED3D9_H

#pragma once

// Direct3D 9 backend: a thin forwarder onto the real device. Running the
// Windows client through this wrapper validates the RHI abstraction before
// any other backend exists (docs/PORT_POSIX_PLAN.md, phase 5).

#ifdef _WIN32

#include "RHIDevice.h"

/// Wraps an IDirect3DVertexBuffer9; Release() drops the COM reference and
/// destroys the wrapper (single-owner semantics, see RHIBuffers.h).
class RHIVertexBufferD3D9 : public IRHIVertexBuffer
{
public:
	RHIVertexBufferD3D9(LPDIRECT3DVERTEXBUFFER9 pBuffer, UINT length, DWORD fvf)
		: m_pBuffer(pBuffer), m_nLength(length), m_dwFVF(fvf)
	{
	}

	HRESULT Lock(UINT offsetToLock, UINT sizeToLock, void** ppData, DWORD flags) override
	{
		return m_pBuffer->Lock(offsetToLock, sizeToLock, ppData, flags);
	}

	HRESULT Unlock() override
	{
		return m_pBuffer->Unlock();
	}

	ULONG Release() override
	{
		m_pBuffer->Release();
		delete this;
		return 0;
	}

	UINT Length() const override
	{
		return m_nLength;
	}

	DWORD FVF() const override
	{
		return m_dwFVF;
	}

	LPDIRECT3DVERTEXBUFFER9 D3DBuffer() const
	{
		return m_pBuffer;
	}

protected:
	LPDIRECT3DVERTEXBUFFER9 m_pBuffer;
	UINT m_nLength;
	DWORD m_dwFVF;
};

class RHIIndexBufferD3D9 : public IRHIIndexBuffer
{
public:
	RHIIndexBufferD3D9(LPDIRECT3DINDEXBUFFER9 pBuffer, UINT length, D3DFORMAT format)
		: m_pBuffer(pBuffer), m_nLength(length), m_eFormat(format)
	{
	}

	HRESULT Lock(UINT offsetToLock, UINT sizeToLock, void** ppData, DWORD flags) override
	{
		return m_pBuffer->Lock(offsetToLock, sizeToLock, ppData, flags);
	}

	HRESULT Unlock() override
	{
		return m_pBuffer->Unlock();
	}

	ULONG Release() override
	{
		m_pBuffer->Release();
		delete this;
		return 0;
	}

	UINT Length() const override
	{
		return m_nLength;
	}

	D3DFORMAT Format() const override
	{
		return m_eFormat;
	}

	LPDIRECT3DINDEXBUFFER9 D3DBuffer() const
	{
		return m_pBuffer;
	}

protected:
	LPDIRECT3DINDEXBUFFER9 m_pBuffer;
	UINT m_nLength;
	D3DFORMAT m_eFormat;
};

class RHIDeviceD3D9 : public IRHIDevice
{
public:
	explicit RHIDeviceD3D9(LPDIRECT3DDEVICE9 pDevice) : m_pDevice(pDevice)
	{
	}

	HRESULT Clear(DWORD flags, D3DCOLOR color, float z, DWORD stencil) override
	{
		return m_pDevice->Clear(0, nullptr, flags, color, z, stencil);
	}

	HRESULT BeginScene() override
	{
		return m_pDevice->BeginScene();
	}

	HRESULT EndScene() override
	{
		return m_pDevice->EndScene();
	}

	HRESULT Present() override
	{
		return m_pDevice->Present(nullptr, nullptr, nullptr, nullptr);
	}

	HRESULT SetRenderState(DWORD state, DWORD value) override
	{
		return m_pDevice->SetRenderState(static_cast<D3DRENDERSTATETYPE>(state), value);
	}

	HRESULT GetRenderState(DWORD state, DWORD* pValue) override
	{
		return m_pDevice->GetRenderState(static_cast<D3DRENDERSTATETYPE>(state), pValue);
	}

	HRESULT SetTextureStageState(DWORD stage, DWORD type, DWORD value) override
	{
		return m_pDevice->SetTextureStageState(
			stage, static_cast<D3DTEXTURESTAGESTATETYPE>(type), value);
	}

	HRESULT GetTextureStageState(DWORD stage, DWORD type, DWORD* pValue) override
	{
		return m_pDevice->GetTextureStageState(
			stage, static_cast<D3DTEXTURESTAGESTATETYPE>(type), pValue);
	}

	HRESULT SetSamplerState(DWORD sampler, DWORD type, DWORD value) override
	{
		return m_pDevice->SetSamplerState(sampler, static_cast<D3DSAMPLERSTATETYPE>(type), value);
	}

	HRESULT GetSamplerState(DWORD sampler, DWORD type, DWORD* pValue) override
	{
		return m_pDevice->GetSamplerState(sampler, static_cast<D3DSAMPLERSTATETYPE>(type), pValue);
	}

	HRESULT SetTransform(DWORD state, const _D3DMATRIX* pMatrix) override
	{
		return m_pDevice->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(state), pMatrix);
	}

	HRESULT GetTransform(DWORD state, _D3DMATRIX* pMatrix) override
	{
		return m_pDevice->GetTransform(static_cast<D3DTRANSFORMSTATETYPE>(state), pMatrix);
	}

	HRESULT SetMaterial(const _D3DMATERIAL9* pMaterial) override
	{
		return m_pDevice->SetMaterial(pMaterial);
	}

	HRESULT SetLight(DWORD index, const _D3DLIGHT9* pLight) override
	{
		return m_pDevice->SetLight(index, pLight);
	}

	HRESULT GetLight(DWORD index, _D3DLIGHT9* pLight) override
	{
		return m_pDevice->GetLight(index, pLight);
	}

	HRESULT LightEnable(DWORD index, BOOL bEnable) override
	{
		return m_pDevice->LightEnable(index, bEnable);
	}

	HRESULT GetLightEnable(DWORD index, BOOL* pbEnabled) override
	{
		return m_pDevice->GetLightEnable(index, pbEnabled);
	}

	HRESULT SetViewport(const D3DVIEWPORT9* pViewport) override
	{
		return m_pDevice->SetViewport(pViewport);
	}

	HRESULT GetViewport(D3DVIEWPORT9* pViewport) override
	{
		return m_pDevice->GetViewport(pViewport);
	}

	HRESULT SetTexture(DWORD stage, LPDIRECT3DBASETEXTURE9 pTexture) override
	{
		return m_pDevice->SetTexture(stage, pTexture);
	}

	HRESULT SetFVF(DWORD fvf) override
	{
		return m_pDevice->SetFVF(fvf);
	}

	HRESULT GetFVF(DWORD* pFvf) override
	{
		return m_pDevice->GetFVF(pFvf);
	}

	HRESULT CreateVertexBuffer(
		UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, IRHIVertexBuffer** ppBuffer) override
	{
		if (ppBuffer == nullptr)
			return RHI_E_FAIL;

		LPDIRECT3DVERTEXBUFFER9 pD3DBuffer = nullptr;
		const HRESULT rval = m_pDevice->CreateVertexBuffer(length, usage, fvf, pool, &pD3DBuffer, nullptr);
		if (FAILED(rval))
			return rval;

		*ppBuffer = new RHIVertexBufferD3D9(pD3DBuffer, length, fvf);
		return rval;
	}

	HRESULT CreateIndexBuffer(
		UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, IRHIIndexBuffer** ppBuffer) override
	{
		if (ppBuffer == nullptr)
			return RHI_E_FAIL;

		LPDIRECT3DINDEXBUFFER9 pD3DBuffer = nullptr;
		const HRESULT rval = m_pDevice->CreateIndexBuffer(length, usage, format, pool, &pD3DBuffer, nullptr);
		if (FAILED(rval))
			return rval;

		*ppBuffer = new RHIIndexBufferD3D9(pD3DBuffer, length, format);
		return rval;
	}

	HRESULT SetStreamSource(
		UINT streamNumber, IRHIVertexBuffer* pBuffer, UINT offsetInBytes, UINT stride) override
	{
		LPDIRECT3DVERTEXBUFFER9 pD3DBuffer =
			pBuffer ? static_cast<RHIVertexBufferD3D9*>(pBuffer)->D3DBuffer() : nullptr;
		return m_pDevice->SetStreamSource(streamNumber, pD3DBuffer, offsetInBytes, stride);
	}

	HRESULT SetIndices(IRHIIndexBuffer* pBuffer) override
	{
		LPDIRECT3DINDEXBUFFER9 pD3DBuffer =
			pBuffer ? static_cast<RHIIndexBufferD3D9*>(pBuffer)->D3DBuffer() : nullptr;
		return m_pDevice->SetIndices(pD3DBuffer);
	}

	HRESULT DrawPrimitive(
		D3DPRIMITIVETYPE primitiveType, UINT startVertex, UINT primitiveCount) override
	{
		return m_pDevice->DrawPrimitive(primitiveType, startVertex, primitiveCount);
	}

	HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE primitiveType, INT baseVertexIndex,
		UINT minVertexIndex, UINT numVertices, UINT startIndex, UINT primitiveCount) override
	{
		return m_pDevice->DrawIndexedPrimitive(
			primitiveType, baseVertexIndex, minVertexIndex, numVertices, startIndex, primitiveCount);
	}

	HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE primitiveType, UINT primitiveCount,
		const void* pVertexData, UINT vertexStride) override
	{
		return m_pDevice->DrawPrimitiveUP(primitiveType, primitiveCount, pVertexData, vertexStride);
	}

	HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE primitiveType, UINT minVertexIndex,
		UINT numVertices, UINT primitiveCount, const void* pIndexData, D3DFORMAT indexFormat,
		const void* pVertexData, UINT vertexStride) override
	{
		return m_pDevice->DrawIndexedPrimitiveUP(primitiveType, minVertexIndex, numVertices,
			primitiveCount, pIndexData, indexFormat, pVertexData, vertexStride);
	}

protected:
	LPDIRECT3DDEVICE9 m_pDevice; // borrowed; owned by CN3Eng
};

#endif // _WIN32

#endif // N3BASE_RHI_RHIDEVICED3D9_H
