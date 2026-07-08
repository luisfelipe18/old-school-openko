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
	return (pMaterial != nullptr) ? D3D_OK : RHI_E_FAIL;
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

HRESULT RHIDeviceNull::SetTexture(DWORD /*stage*/, LPDIRECT3DBASETEXTURE9 /*pTexture*/)
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

HRESULT RHIDeviceNull::SetStreamSource(
	UINT /*streamNumber*/, LPDIRECT3DVERTEXBUFFER9 /*pBuffer*/, UINT /*offsetInBytes*/, UINT /*stride*/)
{
	return D3D_OK;
}

HRESULT RHIDeviceNull::SetIndices(LPDIRECT3DINDEXBUFFER9 /*pBuffer*/)
{
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
