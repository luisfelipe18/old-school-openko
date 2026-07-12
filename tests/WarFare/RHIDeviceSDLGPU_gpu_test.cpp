// Integration test for the SDL_GPU RHI backend (docs/PORT_POSIX_PLAN.md,
// F6b): exercises the exact path the game's UI uses - XYZRHW quads drawn as
// triangle fans with MODULATE(TEXTURE, DIFFUSE) - against a real GPU device
// (Vulkan/lavapipe in CI). Skips cleanly when no GPU/display is available.

#include <gtest/gtest.h>

#include <RHIDeviceSDLGPU.h>

#include <SDL3/SDL.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

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

// Stress case modelled on the in-world workload (~2000 draws and megabytes
// of streamed vertices/uniforms per frame, vs ~25 draws at the login
// screen): every draw pushes distinct uniforms (TFACTOR color) and half of
// them sample a texture, as indexed-fan quads like the game's UI/shapes.
// Verifies several scattered quads landed with their exact color, which
// catches per-draw uniform/arena-offset corruption that only shows up at
// scale.
TEST_F(SdlGpuDeviceTest, ManyDrawsKeepPerDrawUniformsAndTexturesStraight)
{
	// A solid-white 8x8 texture: MODULATE(TEXTURE, TFACTOR-as-diffuse)
	// leaves the TFACTOR color intact, so textured and untextured quads
	// must come out identical if everything is wired right.
	IRHITexture* pWhite = nullptr;
	ASSERT_EQ(m_pDevice->CreateTexture(8, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pWhite),
		D3D_OK);
	D3DLOCKED_RECT lr = {};
	ASSERT_EQ(pWhite->LockRect(0, &lr, nullptr, 0), D3D_OK);
	std::memset(lr.pBits, 0xFF, static_cast<size_t>(lr.Pitch) * 8);
	pWhite->UnlockRect(0);

	int w = 0, h = 0;
	SDL_GetWindowSizeInPixels(m_pWindow, &w, &h);

	constexpr int GRID_COLS = 50, GRID_ROWS = 40; // 2000 draws
	const float cellW = static_cast<float>(w) / GRID_COLS;
	const float cellH = static_cast<float>(h) / GRID_ROWS;

	m_pDevice->BeginScene();
	m_pDevice->Clear(D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 1.0f, 0);

	const auto CellColor = [](int index) -> uint32_t
	{
		// Distinct, easily-recomputable per-cell color.
		const uint8_t r = static_cast<uint8_t>((index * 7) & 0xFF);
		const uint8_t g = static_cast<uint8_t>((index * 13) & 0xFF);
		const uint8_t b = static_cast<uint8_t>((index * 29) & 0xFF);
		return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
	};

	for (int index = 0; index < GRID_COLS * GRID_ROWS; ++index)
	{
		const int col  = index % GRID_COLS;
		const int row  = index / GRID_COLS;
		const float x0 = col * cellW, y0 = row * cellH;
		const float x1 = x0 + cellW, y1 = y0 + cellH;

		// Color arrives via TFACTOR so every draw carries distinct FS
		// uniforms (the per-draw snapshot under test).
		m_pDevice->SetRenderState(D3DRS_TEXTUREFACTOR, CellColor(index));
		const bool bTextured = (index % 2) == 0;
		m_pDevice->SetTexture(0, bTextured ? pWhite : nullptr);
		m_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		m_pDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);

		const QuadVertex vertices[4] = {
			{ x0, y0, 0.5f, 1.0f, 0xFFFFFFFF, 0.0f, 0.0f },
			{ x1, y0, 0.5f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f },
			{ x1, y1, 0.5f, 1.0f, 0xFFFFFFFF, 1.0f, 1.0f },
			{ x0, y1, 0.5f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f },
		};
		const uint16_t indices[4] = { 0, 1, 2, 3 }; // fan, like the game's quads
		m_pDevice->DrawIndexedPrimitiveUP(
			D3DPT_TRIANGLEFAN, 0, 4, 2, indices, D3DFMT_INDEX16, vertices, sizeof(QuadVertex));
	}

	m_pDevice->EndScene();
	m_pDevice->Present();

	// Read the full frame back and verify scattered cells kept their color.
	const std::string dumpPath =
		(std::filesystem::temp_directory_path() / "sdlgpu_stress.ppm").string();
	ASSERT_TRUE(m_pDevice->DumpFramePPM(dumpPath.c_str()));

	std::ifstream ppm(dumpPath, std::ios::binary);
	std::string magic;
	int pw = 0, ph = 0, maxval = 0;
	ppm >> magic >> pw >> ph >> maxval;
	ppm.get(); // single whitespace after the header
	ASSERT_EQ(magic, "P6");
	ASSERT_EQ(pw, w);
	std::vector<uint8_t> pixels(static_cast<size_t>(pw) * ph * 3);
	ppm.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));

	for (int index : { 0, 1, 51, 777, 1000, 1234, 1998, 1999 })
	{
		const int col = index % GRID_COLS;
		const int row = index / GRID_COLS;
		const int px  = static_cast<int>(col * cellW + cellW / 2);
		const int py  = static_cast<int>(row * cellH + cellH / 2);
		const size_t at = (static_cast<size_t>(py) * pw + px) * 3;

		const uint32_t expected = CellColor(index);
		const int er = (expected >> 16) & 0xFF, eg = (expected >> 8) & 0xFF, eb = expected & 0xFF;
		EXPECT_NEAR(pixels[at + 0], er, 2) << "cell " << index;
		EXPECT_NEAR(pixels[at + 1], eg, 2) << "cell " << index;
		EXPECT_NEAR(pixels[at + 2], eb, 2) << "cell " << index;
	}

	std::error_code ec;
	std::filesystem::remove(dumpPath, ec);
	pWhite->Release();
}
