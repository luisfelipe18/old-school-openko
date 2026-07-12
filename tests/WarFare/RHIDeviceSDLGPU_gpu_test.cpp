// Integration test for the SDL_GPU RHI backend (docs/PORT_POSIX_PLAN.md,
// F6b): exercises the exact path the game's UI uses - XYZRHW quads drawn as
// triangle fans with MODULATE(TEXTURE, DIFFUSE) - against a real GPU device
// (Vulkan/lavapipe in CI). Skips cleanly when no GPU/display is available.

#include <gtest/gtest.h>

#include <RHIDeviceSDLGPU.h>

#include <SDL3/SDL.h>

#include <cstring>

namespace
{
struct QuadVertex
{
	float x, y, z, rhw;
	uint32_t color; // D3DCOLOR (BGRA in memory)
	float u, v;
};

constexpr DWORD QUAD_FVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

class SdlGpuDeviceTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		if (!SDL_Init(SDL_INIT_VIDEO))
			GTEST_SKIP() << "no video driver: " << SDL_GetError();

		m_pWindow = SDL_CreateWindow("rhi-sdlgpu-test", 256, 256, 0);
		if (m_pWindow == nullptr)
			GTEST_SKIP() << "no window: " << SDL_GetError();

		m_pDevice = new RHIDeviceSDLGPU(m_pWindow, /*vsync=*/false);
		if (!m_pDevice->IsValid())
		{
			delete m_pDevice;
			m_pDevice = nullptr;
			GTEST_SKIP() << "no SDL_GPU device (Vulkan/Metal driver missing)";
		}

		// The state the engine's SetDefaultEnvironment would install for a
		// plain textured UI draw.
		m_pDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
		m_pDevice->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
		m_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		m_pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
		m_pDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);
		m_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
		m_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
		m_pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
		m_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		m_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		m_pDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		m_pDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		m_pDevice->SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
		m_pDevice->SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		for (DWORD stage = 0; stage < 3; ++stage)
		{
			m_pDevice->SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_POINT);
			m_pDevice->SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
			m_pDevice->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
			m_pDevice->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
			m_pDevice->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		}
		m_pDevice->SetFVF(QUAD_FVF);
	}

	void TearDown() override
	{
		delete m_pDevice;
		if (m_pWindow != nullptr)
			SDL_DestroyWindow(m_pWindow);
		SDL_Quit();
	}

	// Draws a window-covering fan quad (the UI's standard draw) with the
	// given texture, flushes the frame, and samples the center pixel.
	void DrawTexturedQuadAndRead(IRHITexture* pTexture, uint8_t rgbaOut[4])
	{
		int w = 0, h = 0;
		SDL_GetWindowSizeInPixels(m_pWindow, &w, &h);
		const float fw = static_cast<float>(w), fh = static_cast<float>(h);

		const QuadVertex vertices[4] = {
			{ 0.0f, 0.0f, 0.5f, 1.0f, 0xFFFFFFFF, 0.0f, 0.0f },
			{ fw, 0.0f, 0.5f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f },
			{ fw, fh, 0.5f, 1.0f, 0xFFFFFFFF, 1.0f, 1.0f },
			{ 0.0f, fh, 0.5f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f },
		};

		m_pDevice->BeginScene();
		m_pDevice->Clear(D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 1.0f, 0);
		m_pDevice->SetTexture(0, pTexture);
		m_pDevice->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, vertices, sizeof(QuadVertex));
		m_pDevice->EndScene();
		m_pDevice->Present();

		ASSERT_TRUE(m_pDevice->ReadCenterPixel(rgbaOut));
	}

	SDL_Window* m_pWindow      = nullptr;
	RHIDeviceSDLGPU* m_pDevice = nullptr;
};
} // namespace

TEST_F(SdlGpuDeviceTest, DrawsBc1TextureLikeTheGameUI)
{
	// 4x4 DXT1, one solid-red block: color0 = color1 = RGB565 red, all
	// indices 0.
	IRHITexture* pTexture = nullptr;
	ASSERT_EQ(m_pDevice->CreateTexture(4, 4, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &pTexture),
		D3D_OK);

	D3DLOCKED_RECT lr = {};
	ASSERT_EQ(pTexture->LockRect(0, &lr, nullptr, 0), D3D_OK);
	const uint8_t block[8] = { 0x00, 0xF8, 0x00, 0xF8, 0x00, 0x00, 0x00, 0x00 };
	std::memcpy(lr.pBits, block, sizeof(block));
	pTexture->UnlockRect(0);

	uint8_t rgba[4] = {};
	DrawTexturedQuadAndRead(pTexture, rgba);
	EXPECT_GT(rgba[0], 240) << "expected red from the BC1 texture";
	EXPECT_LT(rgba[1], 16);
	EXPECT_LT(rgba[2], 16);

	pTexture->Release();
}

TEST_F(SdlGpuDeviceTest, DrawsBgra8TextureLikeTheGameUI)
{
	IRHITexture* pTexture = nullptr;
	ASSERT_EQ(m_pDevice->CreateTexture(8, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pTexture),
		D3D_OK);

	D3DLOCKED_RECT lr = {};
	ASSERT_EQ(pTexture->LockRect(0, &lr, nullptr, 0), D3D_OK);
	for (int y = 0; y < 8; ++y)
	{
		uint8_t* pRow = static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch;
		for (int x = 0; x < 8; ++x)
		{
			pRow[x * 4 + 0] = 0x00; // B
			pRow[x * 4 + 1] = 0xFF; // G
			pRow[x * 4 + 2] = 0x00; // R
			pRow[x * 4 + 3] = 0xFF; // A
		}
	}
	pTexture->UnlockRect(0);

	uint8_t rgba[4] = {};
	DrawTexturedQuadAndRead(pTexture, rgba);
	EXPECT_LT(rgba[0], 16);
	EXPECT_GT(rgba[1], 240) << "expected green from the BGRA8 texture";
	EXPECT_LT(rgba[2], 16);

	pTexture->Release();
}

TEST_F(SdlGpuDeviceTest, MipmappedTextureSamplesBaseLevel)
{
	IRHITexture* pTexture = nullptr;
	ASSERT_EQ(m_pDevice->CreateTexture(8, 8, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pTexture),
		D3D_OK);
	ASSERT_EQ(pTexture->GetLevelCount(), 2u);

	for (UINT level = 0; level < 2; ++level)
	{
		D3DSURFACE_DESC desc = {};
		pTexture->GetLevelDesc(level, &desc);
		D3DLOCKED_RECT lr = {};
		ASSERT_EQ(pTexture->LockRect(level, &lr, nullptr, 0), D3D_OK);
		for (UINT y = 0; y < desc.Height; ++y)
		{
			uint8_t* pRow = static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch;
			for (UINT x = 0; x < desc.Width; ++x)
			{
				pRow[x * 4 + 0] = (level == 0) ? 0xFF : 0x00; // blue base, black mip
				pRow[x * 4 + 1] = 0x00;
				pRow[x * 4 + 2] = 0x00;
				pRow[x * 4 + 3] = 0xFF;
			}
		}
		pTexture->UnlockRect(level);
	}

	uint8_t rgba[4] = {};
	DrawTexturedQuadAndRead(pTexture, rgba);
	EXPECT_GT(rgba[2], 240) << "expected blue from mip level 0";

	pTexture->Release();
}
