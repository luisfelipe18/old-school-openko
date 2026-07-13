#ifndef N3BASE_RHI_RHIDEVICENULL_H
#define N3BASE_RHI_RHIDEVICENULL_H

#pragma once

// Headless IRHIDevice: stores state so Get* calls round-trip and counts
// draws, but renders nothing. Used by the POSIX build until the GL/SDL_GPU
// backends land (phases 6/6b), by the --smoke CI runs, and by unit tests.

#include "RHIDevice.h"

#include <map>
#include <vector>

/// System-memory buffer: Lock() hands out real storage, so asset loading and
/// geometry generation run headlessly (tests, CI, POSIX until GL lands).
class RHIVertexBufferNull : public IRHIVertexBuffer
{
public:
	RHIVertexBufferNull(UINT length, DWORD fvf) : m_Storage(length), m_dwFVF(fvf)
	{
	}

	HRESULT Lock(UINT offsetToLock, UINT sizeToLock, void** ppData, DWORD flags) override;
	HRESULT Unlock() override;
	ULONG Release() override;

	UINT Length() const override
	{
		return static_cast<UINT>(m_Storage.size());
	}

	DWORD FVF() const override
	{
		return m_dwFVF;
	}

	// Backends that stream from system memory (SDL_GPU) read the CPU copy at
	// draw time instead of keeping a GPU twin of every buffer.
	const std::vector<uint8_t>& StorageBytes() const
	{
		return m_Storage;
	}

protected:
	std::vector<uint8_t> m_Storage;
	DWORD m_dwFVF   = 0;
	bool m_bLocked  = false;
};

/// System-memory texture: each mip level gets a real buffer sized like a D3D9
/// LockRect surface (4x4 block layout for DXT), so N3Texture::Load() can read
/// the .dxt payload into it headlessly (tests, CI, POSIX until GL lands).
class RHITextureNull : public IRHITexture
{
public:
	RHITextureNull(UINT width, UINT height, UINT levels, D3DFORMAT format);

	HRESULT LockRect(
		UINT level, D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD flags) override;
	HRESULT UnlockRect(UINT level) override;
	HRESULT GetLevelDesc(UINT level, D3DSURFACE_DESC* pDesc) override;

	UINT GetLevelCount() const override
	{
		return static_cast<UINT>(m_Levels.size());
	}

	ULONG Release() override;

protected:
	struct Level
	{
		UINT width;
		UINT height;
		INT pitch; // bytes per row (of blocks, for DXT)
		std::vector<uint8_t> storage;
	};

	std::vector<Level> m_Levels;
	D3DFORMAT m_eFormat;
};

class RHIIndexBufferNull : public IRHIIndexBuffer
{
public:
	RHIIndexBufferNull(UINT length, D3DFORMAT format) : m_Storage(length), m_eFormat(format)
	{
	}

	HRESULT Lock(UINT offsetToLock, UINT sizeToLock, void** ppData, DWORD flags) override;
	HRESULT Unlock() override;
	ULONG Release() override;

	UINT Length() const override
	{
		return static_cast<UINT>(m_Storage.size());
	}

	D3DFORMAT Format() const override
	{
		return m_eFormat;
	}

	// See RHIVertexBufferNull::StorageBytes().
	const std::vector<uint8_t>& StorageBytes() const
	{
		return m_Storage;
	}

protected:
	std::vector<uint8_t> m_Storage;
	D3DFORMAT m_eFormat = D3DFMT_INDEX16;
	bool m_bLocked      = false;
};

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
	HRESULT ValidateDevice(DWORD* pNumPasses) override;
	HRESULT SetScissorRect(const RECT* pRect) override;

	// Diagnostics for tests: the last rect passed to SetScissorRect.
	RECT ScissorRect() const
	{
		return m_ScissorRect;
	}

	HRESULT SetTexture(DWORD stage, IRHITexture* pTexture) override;
	HRESULT SetFVF(DWORD fvf) override;
	HRESULT GetFVF(DWORD* pFvf) override;

	HRESULT CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format,
		D3DPOOL pool, IRHITexture** ppTexture) override;

	HRESULT CreateVertexBuffer(
		UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, IRHIVertexBuffer** ppBuffer) override;
	HRESULT CreateIndexBuffer(
		UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, IRHIIndexBuffer** ppBuffer) override;

	HRESULT SetStreamSource(
		UINT streamNumber, IRHIVertexBuffer* pBuffer, UINT offsetInBytes, UINT stride) override;
	HRESULT SetIndices(IRHIIndexBuffer* pBuffer) override;

	IRHIVertexBuffer* BoundVertexBuffer() const
	{
		return m_pBoundVB;
	}
	IRHIIndexBuffer* BoundIndexBuffer() const
	{
		return m_pBoundIB;
	}

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
	RECT m_ScissorRect      = {};
	_D3DMATERIAL9 m_Material = {};
	DWORD m_dwFVF           = 0;
	IRHIVertexBuffer* m_pBoundVB = nullptr;
	IRHIIndexBuffer* m_pBoundIB  = nullptr;
	int m_nDrawCalls        = 0;
	int m_nPresents         = 0;
};

#endif // N3BASE_RHI_RHIDEVICENULL_H
