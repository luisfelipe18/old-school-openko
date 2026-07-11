// SDL3 entry point for POSIX platforms (docs/PORT_POSIX_PLAN.md, phase 3).
//
// Mirrors the structure of the Win32 WinMain/WndProcMain pair: options are
// loaded, the main window is created from them, and the loop alternates
// event handling with per-frame ticking. Until the RHI phases land, the
// per-frame work is a diagnostics pass (input logged as DIK codes, mouse
// flags, focus/wheel/quit events) instead of CGameProcedure - the hookup
// points are marked below.
//
// A --smoke <frames> flag runs the whole stack headlessly (SDL's "dummy"
// video driver) for CI: window creation, event pump, input sampling and
// clean shutdown.

#include "StdAfx.h"
#include "APISocket.h"
#include "CursorDecoder.h"
#include "GameOptions.h"
#include "GameProcedure.h"
#include "GameProcLogIn.h"
#include "LocalInput.h"
#include "LocalInputSDL.h"
#include "RHIDeviceGL.h"
#include "TestScene.h"

#include <N3Base/LogWriter.h>
#include <N3Base/N3Base.h>
#include <N3Base/RHI/RHIDeviceNull.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>

namespace
{
SDL_Window* g_pWindow      = nullptr;
SDL_Cursor* g_pCursor      = nullptr;
bool g_bWindowInFocus      = true;
bool g_bQuitRequested      = false;

// Replaces the LoadCursor(IDC_...) resource path: the .cur files ship next
// to the game data and are decoded into an SDL color cursor.
void SetupWindowCursor()
{
	if (!CN3Base::s_Options.bWindowCursor)
		return; // the game draws its own software cursor (CGameCursor)

	const DecodedCursor decoded =
		LoadCursorFromFile(std::filesystem::path(CN3Base::PathGet()) / "Cursor_Normal.cur");
	if (!decoded.IsValid())
	{
		spdlog::info("Cursor_Normal.cur not found/decodable; keeping the system cursor");
		return;
	}

	SDL_Surface* pSurface = SDL_CreateSurfaceFrom(decoded.width, decoded.height,
		SDL_PIXELFORMAT_RGBA32, const_cast<uint8_t*>(decoded.pixelsRgba.data()),
		decoded.width * 4);
	if (pSurface == nullptr)
		return;

	g_pCursor = SDL_CreateColorCursor(pSurface, decoded.hotspotX, decoded.hotspotY);
	SDL_DestroySurface(pSurface);

	if (g_pCursor != nullptr)
		SDL_SetCursor(g_pCursor);
}

void HandleEvent(const SDL_Event& event)
{
	switch (event.type)
	{
		// WM_CLOSE / WM_QUIT: once CGameProcedure runs here, this routes
		// through CGameProcMain::RequestExit() (combat exit timer included).
		case SDL_EVENT_QUIT:
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			spdlog::info("quit requested");
			g_bQuitRequested = true;
			break;

		// WM_ACTIVATE(WA_ACTIVE/WA_INACTIVE): tracks focus; fullscreen games
		// historically quit on focus loss (WndProcMain posted WM_QUIT).
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			spdlog::info("window focus gained");
			g_bWindowInFocus = true;
			// TODO(F6+): CGameProcedure::s_bIsWindowInFocus = true; refocus edit control
			break;

		case SDL_EVENT_WINDOW_FOCUS_LOST:
			spdlog::info("window focus lost");
			g_bWindowInFocus = false;
			// TODO(F6+): in fullscreen this exits like WA_INACTIVE did
			break;

		// WM_MOUSEWHEEL: same delta scale as Windows (WHEEL_DELTA units).
		case SDL_EVENT_MOUSE_WHEEL:
		{
			const float delta = event.wheel.y * 120.0f; // SDL notches -> WHEEL_DELTA
			spdlog::info("mouse wheel delta={}", delta);
			// TODO(F6+): route to focused msgbox/UI, else CameraZoom(delta * 0.05f)
			break;
		}

		default:
			break;
	}
}

void LogInputDiagnostics(CLocalInput& input)
{
	for (int dik = 0; dik < NUMDIKEYS; ++dik)
	{
		if (input.IsKeyPress(dik))
			spdlog::info("key press   DIK 0x{:02X}", dik);
		if (input.IsKeyPressed(dik))
			spdlog::info("key release DIK 0x{:02X}", dik);
	}

	static int s_nLastMouseFlag = 0;
	const int nMouseFlag        = input.MouseGetFlag();
	if (nMouseFlag != s_nLastMouseFlag)
	{
		const POINT pt = input.MouseGetPos();
		spdlog::info("mouse flags=0x{:03X} pos=({}, {})", nMouseFlag, pt.x, pt.y);
		s_nLastMouseFlag = nMouseFlag;
	}
}
} // namespace

int main(int argc, char* argv[])
{
	// CI/diagnostics: --smoke <N> pumps N frames and exits.
	// --renderer <gl|null> overrides the Option.ini backend for a quick test.
	// --test-scene draws the RHI diagnostic scene (docs/PORT_POSIX_PLAN.md,
	// T6.6/T6.7): gradient background, spinning triangle, lit textured quad.
	long smokeFrames             = -1;
	bool bTestScene              = false;
	bool bLoginScene             = false;
	std::string rendererOverride;
	std::string dumpFramePath;
	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--smoke") == 0 && i + 1 < argc)
			smokeFrames = std::strtol(argv[i + 1], nullptr, 10);
		else if (std::strcmp(argv[i], "--renderer") == 0 && i + 1 < argc)
			rendererOverride = argv[i + 1];
		else if (std::strcmp(argv[i], "--test-scene") == 0)
			bTestScene = true;
		else if (std::strcmp(argv[i], "--dump-frame") == 0 && i + 1 < argc)
			dumpFramePath = argv[i + 1];
		// --scene login brings up the real game procedure (login screen). It
		// needs the game data files present, so it stays opt-in; the default
		// diagnostics path is what CI smoke exercises.
		else if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc
			&& std::strcmp(argv[i + 1], "login") == 0)
			bLoginScene = true;
	}

	LoadGameOptions();

	bool bWantGL = CN3Base::s_Options.bPreferGLRenderer;
	if (rendererOverride == "gl" || rendererOverride == "GL")
		bWantGL = true;
	else if (rendererOverride == "null" || rendererOverride == "Null")
		bWantGL = false;

	srand((uint32_t) time(nullptr));

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		CLogWriter::Write("Cannot initialize SDL: {}", SDL_GetError());
		return -1;
	}

	// 메인 윈도우를 만들고..
	// The game engine works in logical pixel coordinates throughout (UI layout,
	// XYZRHW vertices, viewport). HIGH_PIXEL_DENSITY would give us a 2x
	// framebuffer on Retina displays, mismatching all those coordinates.
	// Omitting it keeps framebuffer == logical size, matching D3D9 behaviour.
	SDL_WindowFlags windowFlags = 0;
	if (!CN3Base::s_Options.bWindowMode)
		windowFlags |= SDL_WINDOW_FULLSCREEN;

	// The GL backend needs a GL-capable window, and the context attributes must
	// be set before the window is created.
	if (bWantGL)
	{
		RHIDeviceGL::SetGLWindowAttributes();
		windowFlags |= SDL_WINDOW_OPENGL;
	}

	g_pWindow = SDL_CreateWindow("Knight OnLine Client", CN3Base::s_Options.iViewWidth,
		CN3Base::s_Options.iViewHeight, windowFlags);

	// A GL-capable window can't be created on drivers without OpenGL (e.g. the
	// "dummy" driver CI uses): drop the GL flag, retry, and use the Null backend.
	if (g_pWindow == nullptr && bWantGL)
	{
		spdlog::warn("GL-capable window creation failed ({}); retrying without OpenGL",
			SDL_GetError());
		bWantGL     = false;
		windowFlags &= ~static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL);
		g_pWindow   = SDL_CreateWindow("Knight OnLine Client", CN3Base::s_Options.iViewWidth,
			  CN3Base::s_Options.iViewHeight, windowFlags);
	}

	if (g_pWindow == nullptr)
	{
		CLogWriter::Write("Cannot create window: {}", SDL_GetError());
		SDL_Quit();
		return -1;
	}

	SetupWindowCursor();

	// Render backend: OpenGL when requested and available (draws the clear
	// colour to the window), otherwise the headless Null backend that CI runs
	// (render code executes and is counted, pixels come later).
	std::unique_ptr<IRHIDevice> pRHIDevice;
	bool bUsingGL = false;
	if (bWantGL)
	{
		auto pGL = std::make_unique<RHIDeviceGL>(g_pWindow, CN3Base::s_Options.bVSyncEnabled);
		if (pGL->IsValid())
		{
			pRHIDevice = std::move(pGL);
			bUsingGL   = true;
		}
		else
		{
			spdlog::warn("OpenGL backend unavailable; falling back to the Null backend");
		}
	}
	if (pRHIDevice == nullptr)
		pRHIDevice = std::make_unique<RHIDeviceNull>();

	CN3Base::RHIDeviceSet(pRHIDevice.get());
	spdlog::info("render backend: {}", bUsingGL ? "OpenGL" : "Null");

	CLocalInput localInput;
	localInput.Init(nullptr, nullptr);

	spdlog::info("window created: {}x{} ({})", CN3Base::s_Options.iViewWidth,
		CN3Base::s_Options.iViewHeight,
		CN3Base::s_Options.bWindowMode ? "windowed" : "fullscreen");

	// The real game procedure (login milestone, docs/PORT_POSIX_PLAN.md T6.8).
	// StaticMemberInit builds the engine/tables/UI and the login procedure over
	// the RHI device the entry point already installed; it needs the game data
	// files, hence the opt-in flag.
	if (bLoginScene)
	{
		// StaticMemberInit loads the game's data tables and UI from the working
		// directory (the base path is set to the CWD in LoadGameOptions). If the
		// game data isn't there it fails deep inside with exit(-1); surface a
		// clear reason up front so a missing-data run isn't a silent exit.
		const std::filesystem::path dataDir =
			std::filesystem::path(CN3Base::PathGet()) / "Data";
		std::error_code ec;
		if (!std::filesystem::exists(dataDir, ec))
		{
			spdlog::error(
				"--scene login: game data not found at '{}'. Run KnightOnLine from a "
				"directory that contains the Knight Online client data (Data/, UI/, ...).",
				dataDir.string());
			CN3Base::RHIDeviceSet(nullptr);
			pRHIDevice.reset();
			if (g_pCursor != nullptr)
				SDL_DestroyCursor(g_pCursor);
			SDL_DestroyWindow(g_pWindow);
			SDL_Quit();
			return -1;
		}

		CGameProcedure::StaticMemberInit(nullptr, nullptr);
		CGameProcedure::ProcActiveSet(CGameProcedure::s_pProcLogIn);
		spdlog::info("login scene: CGameProcedure brought up");
	}

	long frame = 0;
	while (!g_bQuitRequested)
	{
		// Pump all pending events, then use the idle time for a frame -
		// the same PeekMessage/TranslateMessage pattern WinMain uses.
		SDL_Event event;
		while (SDL_PollEvent(&event))
			HandleEvent(event);

		if (g_bQuitRequested)
			break;

		localInput.Tick();
		if (!bLoginScene)
			LogInputDiagnostics(localInput);

		// Alt+Enter (Option+Enter or Cmd+Enter on macOS) toggles
		// windowed/fullscreen (ChangeDisplaySettings equivalent).
		if ((localInput.IsKeyDown(DIK_LMENU) || localInput.IsKeyDown(DIK_RMENU)
				|| localInput.IsKeyDown(DIK_LWIN) || localInput.IsKeyDown(DIK_RWIN))
			&& localInput.IsKeyPress(DIK_RETURN))
		{
			CN3Base::s_Options.bWindowMode = !CN3Base::s_Options.bWindowMode;
			SDL_SetWindowFullscreen(g_pWindow, !CN3Base::s_Options.bWindowMode);
			spdlog::info("toggled to {}", CN3Base::s_Options.bWindowMode ? "windowed" : "fullscreen");
		}

		if (!bLoginScene && localInput.IsKeyPressed(DIK_ESCAPE))
			g_bQuitRequested = true;

		if (bLoginScene)
		{
			// The active procedure (login) drives the whole frame: it clears,
			// renders its UI through the RHI, and presents via CN3Eng.
			CGameProcedure::TickActive();
			CGameProcedure::RenderActive();
			if (PlatformQuitRequested()) // BEHAVIOR_EXIT / PostQuitMessage
				g_bQuitRequested = true;
		}
		else
		{
			// Diagnostics frame: the RHI sequence runs with a dim-blue clear so
			// the GL backend is visibly not-black.
			pRHIDevice->BeginScene();
			pRHIDevice->Clear(D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF102040, 1.0f, 0);
			if (bTestScene)
			{
				int pixelW = 0, pixelH = 0;
				SDL_GetWindowSizeInPixels(g_pWindow, &pixelW, &pixelH);
				TestSceneTick(pRHIDevice.get(), static_cast<float>(frame) * 0.02f, pixelW, pixelH);
			}
			pRHIDevice->EndScene();
			pRHIDevice->Present();
		}

		SDL_Delay(g_bWindowInFocus ? 10 : 50);

		++frame;
		if (smokeFrames >= 0 && frame >= smokeFrames)
		{
			spdlog::info("smoke run finished after {} frames", frame);
			break;
		}
	}

	// Clean shutdown: disconnect the game sockets and tear the procedure down
	// (mirrors the Win32 WndProc close path) before the RHI/window go away. The
	// engine's RHI pointer is owned here, so StaticMemberRelease must run while
	// the device is still alive.
	if (bLoginScene)
	{
		if (CGameProcedure::s_pSocket != nullptr)
			CGameProcedure::s_pSocket->Disconnect();
		if (CGameProcedure::s_pSocketSub != nullptr)
			CGameProcedure::s_pSocketSub->Disconnect();
		CGameProcedure::StaticMemberRelease();
	}

	if (auto* pNull = dynamic_cast<RHIDeviceNull*>(pRHIDevice.get()))
		spdlog::info("frames presented through the RHI: {}", pNull->PresentCount());

	// Smoke diagnostics: with the GL backend, prove pixels actually landed by
	// rendering one final frame and sampling its center before the swap
	// (glReadPixels reads the back buffer).
	if (auto* pGL = dynamic_cast<RHIDeviceGL*>(pRHIDevice.get()))
	{
		pGL->BeginScene();
		pGL->Clear(D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF102040, 1.0f, 0);
		if (bTestScene)
		{
			int pixelW = 0, pixelH = 0;
			SDL_GetWindowSizeInPixels(g_pWindow, &pixelW, &pixelH);
			TestSceneTick(pRHIDevice.get(), static_cast<float>(frame) * 0.02f, pixelW, pixelH);
		}
		pGL->EndScene();

		uint8_t rgba[4] = {};
		if (pGL->ReadCenterPixel(rgba))
			spdlog::info("GL center pixel: R={} G={} B={} A={}", rgba[0], rgba[1], rgba[2], rgba[3]);

		if (!dumpFramePath.empty() && pGL->DumpFramePPM(dumpFramePath.c_str()))
			spdlog::info("GL frame dumped to {}", dumpFramePath);
	}

	TestSceneRelease(); // GL resources must go before the context
	CN3Base::RHIDeviceSet(nullptr);
	pRHIDevice.reset(); // destroy the GL context before the window
	if (g_pCursor != nullptr)
		SDL_DestroyCursor(g_pCursor);
	SDL_DestroyWindow(g_pWindow);
	SDL_Quit();

	spdlog::info("clean shutdown");
	return 0;
}
