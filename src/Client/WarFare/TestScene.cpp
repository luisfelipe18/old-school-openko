#include "StdAfx.h"
#include "TestScene.h"

#include <N3Base/My_3DStruct.h>
#include <N3Base/RHI/RHIDevice.h>

#include <cmath>
#include <cstdint>

namespace
{
IRHITexture* g_pCheckerTex = nullptr;

// 64x64 blue/white checkerboard through the RHI (CreateTexture + LockRect),
// the same path N3Texture uses to load .dxt payloads.
IRHITexture* GetCheckerTexture(IRHIDevice* pDevice)
{
	if (g_pCheckerTex != nullptr)
		return g_pCheckerTex;

	if (FAILED(pDevice->CreateTexture(
			64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_pCheckerTex)))
		return nullptr;

	D3DLOCKED_RECT lr = {};
	if (SUCCEEDED(g_pCheckerTex->LockRect(0, &lr, nullptr, 0)))
	{
		for (int y = 0; y < 64; ++y)
		{
			auto* pRow = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch);
			for (int x = 0; x < 64; ++x)
				pRow[x] = (((x / 8) + (y / 8)) & 1) ? 0xFFFFFFFF : 0xFF3060C0;
		}
		g_pCheckerTex->UnlockRect(0);
	}
	return g_pCheckerTex;
}

void DrawBackgroundGradient(IRHIDevice* pDevice, int iW, int iH)
{
	// Pre-transformed full-window quad: exercises the XYZRHW screen-space path.
	pDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
	pDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
	pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	pDevice->SetTexture(0, nullptr);
	pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	pDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);

	const float w = static_cast<float>(iW), h = static_cast<float>(iH);
	const __VertexTransformedColor vertices[4] = {
		{0.0f, 0.0f, 0.5f, 1.0f, 0xFF203050}, // top-left: slate blue
		{w, 0.0f, 0.5f, 1.0f, 0xFF104060},    // top-right
		{w, h, 0.5f, 1.0f, 0xFF206040},       // bottom-right: teal
		{0.0f, h, 0.5f, 1.0f, 0xFF102030},    // bottom-left
	};

	pDevice->SetFVF(FVF_TRANSFORMEDCOLOR);
	pDevice->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, vertices, sizeof(vertices[0]));
}

void DrawSpinningTriangle(IRHIDevice* pDevice, float fTime)
{
	// Vertex-colored geometry: BGRA attrib swizzle + world matrix animation.
	pDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
	pDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
	pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	pDevice->SetTexture(0, nullptr);
	pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	pDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);

	__Matrix44 world;
	world.RotationY(fTime);
	world.PosSet(-1.4f, 0.2f, 0.0f);
	pDevice->SetTransform(D3DTS_WORLD, world.toD3D());

	const __VertexXyzColor vertices[3] = {
		{0.0f, 0.9f, 0.0f, 0xFFFF4040},   // red
		{0.8f, -0.6f, 0.0f, 0xFF40FF40},  // green
		{-0.8f, -0.6f, 0.0f, 0xFF4080FF}, // blue
	};

	pDevice->SetFVF(FVF_XYZCOLOR);
	pDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, vertices, sizeof(vertices[0]));
}

void DrawLitTexturedQuad(IRHIDevice* pDevice, float fTime)
{
	// Textured + lit: checker texture MODULATEd with per-vertex directional
	// lighting (material diffuse), the standard mesh path.
	pDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
	pDevice->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	pDevice->SetRenderState(D3DRS_LIGHTING, TRUE);
	pDevice->SetRenderState(D3DRS_AMBIENT, 0xFF404040);
	pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

	_D3DLIGHT9 light   = {};
	light.Type         = D3DLIGHT_DIRECTIONAL;
	light.Diffuse.r    = 1.0f;
	light.Diffuse.g    = 1.0f;
	light.Diffuse.b    = 0.9f;
	light.Direction.x  = 0.3f;
	light.Direction.y  = -0.5f;
	light.Direction.z  = 0.8f;
	pDevice->SetLight(0, &light);
	pDevice->LightEnable(0, TRUE);

	_D3DMATERIAL9 material = {};
	material.Diffuse.r     = 1.0f;
	material.Diffuse.g     = 1.0f;
	material.Diffuse.b     = 1.0f;
	material.Diffuse.a     = 1.0f;
	material.Ambient       = material.Diffuse;
	pDevice->SetMaterial(&material);

	pDevice->SetTexture(0, GetCheckerTexture(pDevice));
	pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	pDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	pDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	pDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	pDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);

	__Matrix44 world;
	world.RotationY(fTime * 0.6f);
	world.PosSet(0.6f, 0.0f, 0.0f);
	pDevice->SetTransform(D3DTS_WORLD, world.toD3D());

	const __VertexT1 vertices[4] = {
		{-1.0f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f},
		{1.0f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f},
		{1.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f},
		{-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f},
	};
	const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

	pDevice->SetFVF(FVF_VNT1);
	pDevice->DrawIndexedPrimitiveUP(
		D3DPT_TRIANGLELIST, 0, 4, 2, indices, D3DFMT_INDEX16, vertices, sizeof(vertices[0]));
}
} // namespace

void TestSceneTick(IRHIDevice* pDevice, float fTime, int iPixelWidth, int iPixelHeight)
{
	if (pDevice == nullptr)
		return;

	__Matrix44 view, proj;
	view.LookAtLH(__Vector3(0.0f, 1.0f, -4.0f), __Vector3(0.0f, 0.0f, 0.0f),
		__Vector3(0.0f, 1.0f, 0.0f));
	proj.PerspectiveFovLH(0.9f,
		static_cast<float>(iPixelWidth) / static_cast<float>(iPixelHeight > 0 ? iPixelHeight : 1),
		0.1f, 100.0f);
	pDevice->SetTransform(D3DTS_VIEW, view.toD3D());
	pDevice->SetTransform(D3DTS_PROJECTION, proj.toD3D());

	DrawBackgroundGradient(pDevice, iPixelWidth, iPixelHeight);
	DrawSpinningTriangle(pDevice, fTime);
	DrawLitTexturedQuad(pDevice, fTime);
}

void TestSceneRelease()
{
	if (g_pCheckerTex != nullptr)
	{
		g_pCheckerTex->Release();
		g_pCheckerTex = nullptr;
	}
}
