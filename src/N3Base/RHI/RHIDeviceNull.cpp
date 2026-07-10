#include "../StdAfxBase.h"
#include "RHIDeviceNull.h"

#include <cstring>

namespace
{
uint64_t PackKey(DWORD high, DWORD low)
{
	return (uint64_t(high) << 32) | low;
}
} // namespace

HRESULT RHIDeviceNull::Clear(DWORD /*flags*/, D3DCOLOR /*color*/, float /*z*/, DWORD /*stencil*/)
{
	return D3D_OK;
}

HRESULT RHIDeviceNull::BeginScene()
{
	return D3D_OK;
}

HRESULT RHIDeviceNull::EndScene()
{
	return D3D_OK;
}

HRESULT RHIDeviceNull::Present()
{
	++m_nPresents;
	return D3D_OK;
}

HRESULT RHIDeviceNull::SetRenderState(DWORD state, DWORD value)
{
	m_RenderStates[state] = value;
	return D3D_OK;
}

HRESULT RHIDeviceNull::GetRenderState(DWORD state, DWORD* pValue)
{
	if (pValue == nullptr)
		return RHI_E_FAIL;

	const auto it = m_RenderStates.find(state);
	*pValue       = (it != m_RenderStates.end()) ? it->second : 0;
	return D3D_OK;
}

HRESULT RHIDeviceNull::SetTextureStageState(DWORD stage, DWORD type, DWORD value)
{
	m_StageStates[PackKey(stage, type)] = value;
	return D3D_OK;
}

HRESULT RHIDeviceNull::GetTextureStageState(DWORD stage, DWORD type, DWORD* pValue)
{
	if (pValue == nullptr)
		return RHI_E_FAIL;

	const auto it = m_StageStates.find(PackKey(stage, type));
	*pValue       = (it != m_StageStates.end()) ? it->second : 0;
	return D3D_OK;
}

HRESULT RHIDeviceNull::SetSamplerState(DWORD sampler, DWORD type, DWORD value)
{
	m_SamplerStates[PackKey(sampler, type)] = value;
	return D3D_OK;
}

HRESULT RHIDeviceNull::GetSamplerState(DWORD sampler, DWORD type, DWORD* pValue)
{
	if (pValue == nullptr)
		return RHI_E_FAIL;

	const auto it = m_SamplerStates.find(PackKey(sampler, type));
	*pValue       = (it != m_SamplerStates.end()) ? it->second : 0;
	return D3D_OK;
}

HRESULT RHIDeviceNull::SetTransform(DWORD state, const _D3DMATRIX* pMatrix)
{
	if (pMatrix == nullptr)
		return RHI_E_FAIL;

	// _D3DMATRIX and __Matrix44 share the 16-float layout.
	memcpy(&m_Transforms[state], pMatrix, sizeof(__Matrix44));
	return D3D_OK;
}

HRESULT RHIDeviceNull::GetTransform(DWORD state, _D3DMATRIX* pMatrix)
{
	if (pMatrix == nullptr)
		return RHI_E_FAIL;

	const auto it = m_Transforms.find(state);
	if (it != m_Transforms.end())
		memcpy(pMatrix, &it->second, sizeof(__Matrix44));
	else
	{
		const __Matrix44 identity = __Matrix44::GetIdentity();
		memcpy(pMatrix, &identity, sizeof(__Matrix44));
	}

	return D3D_OK;
}

HRESULT RHIDeviceNull::SetMaterial(const _D3DMATERIAL9* pMaterial)
{
	if (pMaterial == nullptr)
		return RHI_E_FAIL;

	m_Material = *pMaterial;
	return D3D_OK;
}

HRESULT RHIDeviceNull::SetLight(DWORD index, const _D3DLIGHT9* pLight)
{
	if (pLight == nullptr)
		return RHI_E_FAIL;

	memcpy(&m_Lights[index], pLight, sizeof(_D3DLIGHT9));
	return D3D_OK;
}

HRESULT RHIDeviceNull::GetLight(DWORD index, _D3DLIGHT9* pLight)
{
	if (pLight == nullptr)
		return RHI_E_FAIL;

	const auto it = m_Lights.find(index);
	if (it == m_Lights.end())
		return RHI_E_FAIL;

	memcpy(pLight, &it->second, sizeof(_D3DLIGHT9));
	return D3D_OK;
}

HRESULT RHIDeviceNull::LightEnable(DWORD index, BOOL bEnable)
{
	m_LightsEnabled[index] = bEnable;
	return D3D_OK;
}

HRESULT RHIDeviceNull::GetLightEnable(DWORD index, BOOL* pbEnabled)
{
	if (pbEnabled == nullptr)
		return RHI_E_FAIL;

	const auto it = m_LightsEnabled.find(index);
	*pbEnabled    = (it != m_LightsEnabled.end()) ? it->second : FALSE;
	return D3D_OK;
}

HRESULT RHIDeviceNull::SetViewport(const D3DVIEWPORT9* pViewport)
{
	if (pViewport == nullptr)
		return RHI_E_FAIL;

	m_Viewport = *pViewport;
	return D3D_OK;
}

HRESULT RHIDeviceNull::GetViewport(D3DVIEWPORT9* pViewport)
{
	if (pViewport == nullptr)
		return RHI_E_FAIL;

	*pViewport = m_Viewport;
	return D3D_OK;
}

HRESULT RHIDeviceNull::ValidateDevice(DWORD* pNumPasses)
{
	if (pNumPasses == nullptr)
		return RHI_E_FAIL;

	// The Null backend never rejects a fixed-function state combination, so
	// the whole pipeline always renders in a single pass.
	*pNumPasses = 1;
	return D3D_OK;
}

HRESULT RHIDeviceNull::SetScissorRect(const RECT* pRect)
{
	if (pRect == nullptr)
		return RHI_E_FAIL;

	m_ScissorRect = *pRect;
	return D3D_OK;
}

HRESULT RHIDeviceNull::SetTexture(DWORD /*stage*/, IRHITexture* /*pTexture*/)
{
	return D3D_OK;
}

HRESULT RHIDeviceNull::SetFVF(DWORD fvf)
{
	m_dwFVF = fvf;
	return D3D_OK;
}

HRESULT RHIDeviceNull::GetFVF(DWORD* pFvf)
{
	if (pFvf == nullptr)
		return RHI_E_FAIL;

	*pFvf = m_dwFVF;
	return D3D_OK;
}

// --- Buffers -----------------------------------------------------------------

namespace
{
template <typename Storage>
HRESULT LockStorage(Storage& storage, bool& locked, UINT offsetToLock, UINT sizeToLock, void** ppData)
{
	if (ppData == nullptr)
		return RHI_E_FAIL;

	// D3D9 semantics: size 0 locks from the offset to the end.
	const size_t end = (sizeToLock == 0) ? storage.size() : size_t(offsetToLock) + sizeToLock;
	if (offsetToLock >= storage.size() || end > storage.size())
		return RHI_E_FAIL;

	locked  = true;
	*ppData = storage.data() + offsetToLock;
	return D3D_OK;
}
} // namespace

HRESULT RHIVertexBufferNull::Lock(UINT offsetToLock, UINT sizeToLock, void** ppData, DWORD /*flags*/)
{
	return LockStorage(m_Storage, m_bLocked, offsetToLock, sizeToLock, ppData);
}

HRESULT RHIVertexBufferNull::Unlock()
{
	m_bLocked = false;
	return D3D_OK;
}

ULONG RHIVertexBufferNull::Release()
{
	delete this;
	return 0;
}

HRESULT RHIIndexBufferNull::Lock(UINT offsetToLock, UINT sizeToLock, void** ppData, DWORD /*flags*/)
{
	return LockStorage(m_Storage, m_bLocked, offsetToLock, sizeToLock, ppData);
}

HRESULT RHIIndexBufferNull::Unlock()
{
	m_bLocked = false;
	return D3D_OK;
}

ULONG RHIIndexBufferNull::Release()
{
	delete this;
	return 0;
}

HRESULT RHIDeviceNull::CreateVertexBuffer(
	UINT length, DWORD /*usage*/, DWORD fvf, D3DPOOL /*pool*/, IRHIVertexBuffer** ppBuffer)
{
	if (ppBuffer == nullptr || length == 0)
		return RHI_E_FAIL;

	*ppBuffer = new RHIVertexBufferNull(length, fvf);
	return D3D_OK;
}

HRESULT RHIDeviceNull::CreateIndexBuffer(
	UINT length, DWORD /*usage*/, D3DFORMAT format, D3DPOOL /*pool*/, IRHIIndexBuffer** ppBuffer)
{
	if (ppBuffer == nullptr || length == 0)
		return RHI_E_FAIL;

	*ppBuffer = new RHIIndexBufferNull(length, format);
	return D3D_OK;
}

// --- Textures ----------------------------------------------------------------

namespace
{
// Bytes for one row of blocks (DXT) or pixels (uncompressed), matching how a
// D3D9 LockRect reports Pitch, plus the number of such rows.
void SurfaceLayout(D3DFORMAT format, UINT width, UINT height, INT& pitch, UINT& rows)
{
	const bool bDXT1 = (format == D3DFMT_DXT1);
	const bool bDXT  = bDXT1 || format == D3DFMT_DXT2 || format == D3DFMT_DXT3
					   || format == D3DFMT_DXT4 || format == D3DFMT_DXT5;
	if (bDXT)
	{
		const UINT blocksX = (width + 3) / 4;
		const UINT blocksY = (height + 3) / 4;
		pitch              = static_cast<INT>(blocksX * (bDXT1 ? 8u : 16u));
		rows               = blocksY;
		return;
	}

	UINT bpp = 4;
	if (format == D3DFMT_A1R5G5B5 || format == D3DFMT_A4R4G4B4)
		bpp = 2;
	else if (format == D3DFMT_R8G8B8)
		bpp = 3;
	pitch = static_cast<INT>(width * bpp);
	rows  = height;
}
} // namespace

RHITextureNull::RHITextureNull(UINT width, UINT height, UINT levels, D3DFORMAT format)
	: m_eFormat(format)
{
	m_Levels.reserve(levels);
	UINT w = width, h = height;
	for (UINT i = 0; i < levels; ++i)
	{
		INT pitch  = 0;
		UINT rows  = 0;
		SurfaceLayout(format, w, h, pitch, rows);

		Level level;
		level.width  = w;
		level.height = h;
		level.pitch  = pitch;
		level.storage.resize(static_cast<size_t>(pitch) * rows);
		m_Levels.push_back(std::move(level));

		w = (w > 1) ? w / 2 : 1;
		h = (h > 1) ? h / 2 : 1;
	}
}

HRESULT RHITextureNull::LockRect(
	UINT level, D3DLOCKED_RECT* pLockedRect, const RECT* /*pRect*/, DWORD /*flags*/)
{
	if (pLockedRect == nullptr || level >= m_Levels.size())
		return RHI_E_FAIL;

	pLockedRect->Pitch = m_Levels[level].pitch;
	pLockedRect->pBits = m_Levels[level].storage.data();
	return D3D_OK;
}

HRESULT RHITextureNull::UnlockRect(UINT level)
{
	return (level < m_Levels.size()) ? D3D_OK : RHI_E_FAIL;
}

HRESULT RHITextureNull::GetLevelDesc(UINT level, D3DSURFACE_DESC* pDesc)
{
	if (pDesc == nullptr || level >= m_Levels.size())
		return RHI_E_FAIL;

	*pDesc                    = {};
	pDesc->Format             = m_eFormat;
	pDesc->Type               = D3DRTYPE_TEXTURE;
	pDesc->Pool               = D3DPOOL_MANAGED;
	pDesc->MultiSampleType    = D3DMULTISAMPLE_NONE;
	pDesc->Width              = m_Levels[level].width;
	pDesc->Height             = m_Levels[level].height;
	return D3D_OK;
}

ULONG RHITextureNull::Release()
{
	delete this;
	return 0;
}

HRESULT RHIDeviceNull::CreateTexture(UINT width, UINT height, UINT levels, DWORD /*usage*/,
	D3DFORMAT format, D3DPOOL /*pool*/, IRHITexture** ppTexture)
{
	if (ppTexture == nullptr || width == 0 || height == 0)
		return RHI_E_FAIL;

	// levels == 0 means "full mip chain" in D3D9.
	if (levels == 0)
	{
		levels    = 1;
		UINT w = width, h = height;
		while (w > 1 || h > 1)
		{
			w = (w > 1) ? w / 2 : 1;
			h = (h > 1) ? h / 2 : 1;
			++levels;
		}
	}

	*ppTexture = new RHITextureNull(width, height, levels, format);
	return D3D_OK;
}

HRESULT RHIDeviceNull::SetStreamSource(
	UINT /*streamNumber*/, IRHIVertexBuffer* pBuffer, UINT /*offsetInBytes*/, UINT /*stride*/)
{
	m_pBoundVB = pBuffer;
	return D3D_OK;
}

HRESULT RHIDeviceNull::SetIndices(IRHIIndexBuffer* pBuffer)
{
	m_pBoundIB = pBuffer;
	return D3D_OK;
}

HRESULT RHIDeviceNull::DrawPrimitive(
	D3DPRIMITIVETYPE /*primitiveType*/, UINT /*startVertex*/, UINT /*primitiveCount*/)
{
	++m_nDrawCalls;
	return D3D_OK;
}

HRESULT RHIDeviceNull::DrawIndexedPrimitive(D3DPRIMITIVETYPE /*primitiveType*/,
	INT /*baseVertexIndex*/, UINT /*minVertexIndex*/, UINT /*numVertices*/, UINT /*startIndex*/,
	UINT /*primitiveCount*/)
{
	++m_nDrawCalls;
	return D3D_OK;
}

HRESULT RHIDeviceNull::DrawPrimitiveUP(D3DPRIMITIVETYPE /*primitiveType*/, UINT /*primitiveCount*/,
	const void* pVertexData, UINT /*vertexStride*/)
{
	if (pVertexData == nullptr)
		return RHI_E_FAIL;

	++m_nDrawCalls;
	return D3D_OK;
}

HRESULT RHIDeviceNull::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE /*primitiveType*/,
	UINT /*minVertexIndex*/, UINT /*numVertices*/, UINT /*primitiveCount*/, const void* pIndexData,
	D3DFORMAT /*indexFormat*/, const void* pVertexData, UINT /*vertexStride*/)
{
	if (pIndexData == nullptr || pVertexData == nullptr)
		return RHI_E_FAIL;

	++m_nDrawCalls;
	return D3D_OK;
}
