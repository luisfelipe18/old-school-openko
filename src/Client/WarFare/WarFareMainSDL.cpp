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
#include "CursorDecoder.h"
#include "GameOptions.h"
#include "LocalInput.h"
#include "LocalInputSDL.h"
#include "RHIDeviceGL.h"

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
	long smokeFrames             = -1;
	std::string rendererOverride;
	for (int i = 1; i < argc - 1; ++i)
	{
		if (std::strcmp(argv[i], "--smoke") == 0)
			smokeFrames = std::strtol(argv[i + 1], nullptr, 10);
		else if (std::strcmp(argv[i], "--renderer") == 0)
			rendererOverride = argv[i + 1];
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
	SDL_WindowFlags windowFlags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
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

	// TODO(F5/F6): CGameProcedure::StaticMemberInit + ProcActiveSet(s_pProcLogIn)

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

		if (localInput.IsKeyPressed(DIK_ESCAPE))
			g_bQuitRequested = true;

		// TODO(F6): CGameProcedure::TickActive() + RenderActive() replace the
		// diagnostics below; the RHI frame sequence already runs here. The
		// clear colour is a dim blue so the GL backend is visibly not-black.
		pRHIDevice->BeginScene();
		pRHIDevice->Clear(D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF102040, 1.0f, 0);
		pRHIDevice->EndScene();
		pRHIDevice->Present();

		SDL_Delay(g_bWindowInFocus ? 10 : 50);

		++frame;
		if (smokeFrames >= 0 && frame >= smokeFrames)
		{
			spdlog::info("smoke run finished after {} frames", frame);
			break;
		}
	}

	// Clean shutdown (the WndProc used to disconnect the game sockets here;
	// that returns with CGameProcedure in the render phases).
	if (auto* pNull = dynamic_cast<RHIDeviceNull*>(pRHIDevice.get()))
		spdlog::info("frames presented through the RHI: {}", pNull->PresentCount());
	CN3Base::RHIDeviceSet(nullptr);
	pRHIDevice.reset(); // destroy the GL context before the window
	if (g_pCursor != nullptr)
		SDL_DestroyCursor(g_pCursor);
	SDL_DestroyWindow(g_pWindow);
	SDL_Quit();

	spdlog::info("clean shutdown");
	return 0;
}
