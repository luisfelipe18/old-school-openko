// POSIX entry point for the Option tool (docs/PORT_POSIX_PLAN.md): the
// original is an MFC dialog (OptionDlg.cpp); MFC is Windows/MSVC-only with
// no POSIX equivalent, so this reimplements the same dialog - same fields,
// same Option.ini/Server.Ini section and key names - on Dear ImGui over the
// SDL3 + OpenGL 3.3 core stack the WarFare client already uses.

#include "OptionCore.h"

#include <Platform/GameDataDir.h>
#include <Platform/IconDecoder.h>
#include <Platform/PlatformPaths.h>
#include <Platform/ProcessLaunch.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <spdlog/spdlog.h>

#include <SDL3/SDL.h>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
// Locates the directory holding Option.ini/Server.Ini via the same
// precedence WarFare uses (Platform/GameDataDir.h, docs/PORT_POSIX_PLAN.md
// F8/F10): --data <path> > OPENKO_GAME_DATA env var > CWD if it looks like
// a data dir > the executable's own directory (and the surrounding .app
// bundle's usual GameData/Resources slots on macOS) > well-known user
// directories. Falls back to CWD when nothing matches, same as before -
// Option still works standalone (with defaults) if no install is found.
fs::path ResolveGameDir(const std::string& dataDirOverride)
{
	fs::path dataDir = FindGameDataDir(dataDirOverride);
	if (!dataDir.empty())
	{
		spdlog::info("Option: game data directory: {}", dataDir.string());
		return dataDir;
	}

	spdlog::warn("Option: no game data directory found (pass --data <path>, set "
				 "OPENKO_GAME_DATA, or run from the directory containing Server.Ini) - "
				 "showing defaults");

	std::error_code ec;
	fs::path cwd = fs::current_path(ec);
	return ec ? fs::path(".") : cwd;
}

std::vector<option_core::Resolution> DetectDisplayResolutions()
{
	std::vector<option_core::Resolution> detected;

	int displayCount            = 0;
	SDL_DisplayID* displays     = SDL_GetDisplays(&displayCount);
	if (displays == nullptr || displayCount <= 0)
		return detected;

	SDL_DisplayID primary = SDL_GetPrimaryDisplay();
	SDL_DisplayID target   = (primary != 0) ? primary : displays[0];

	int modeCount               = 0;
	SDL_DisplayMode** modes     = SDL_GetFullscreenDisplayModes(target, &modeCount);
	if (modes != nullptr)
	{
		for (int i = 0; i < modeCount; ++i)
		{
			if (modes[i] != nullptr)
				detected.push_back(
					{ static_cast<uint32_t>(modes[i]->w), static_cast<uint32_t>(modes[i]->h) });
		}
		SDL_free(modes);
	}
	SDL_free(displays);

	return detected;
}

// Best-effort launch of the game client next to this tool (the original
// launches Launcher.exe, which isn't ported yet - launching WarFare directly
// gets you into the game, matching the self-contained POSIX bundle from F8).
// Returns false (leaving the Option window open so the failure is visible)
// when the binary can't be found - e.g. WarFare ships as a KnightOnLine.app
// bundle on macOS, which FindSiblingExecutable accounts for.
bool LaunchWarFareAndExit(const fs::path& gameDir)
{
	const fs::path candidate = platform_launch::FindSiblingExecutable(
		{ GetExecutableDir(), gameDir }, "KnightOnLine");

	if (candidate.empty())
	{
		spdlog::warn(
			"Option: couldn't find the KnightOnLine binary next to this tool - start it manually.");
		return false;
	}

	spdlog::info("Option: launching {}", candidate.string());
	// Forward the resolved game data dir explicitly (same spirit as
	// StartGame() re-passing its own command line) rather than relying on
	// WarFare's own auto-discovery to land on the same directory - but only
	// when it's a real one; ResolveGameDir() falls back to CWD when
	// discovery finds nothing, and passing that through would override
	// WarFare's own (equally capable) auto-discovery with a guess instead of
	// leaving it to try on its own.
	platform_launch::LaunchDetached(candidate, gameDir);
	return true;
}

// Decodes Option.ico (staged next to the binary by CMake) and applies it as
// the SDL window icon. Best-effort: a missing/undecodable file just leaves
// the window manager's default icon, same as any other resource lookup miss
// in this tool.
void ApplyWindowIcon(SDL_Window* pWindow)
{
	const fs::path iconPath = GetExecutableDir() / "Option.ico";
	const DecodedIcon icon  = LoadIconFromFile(iconPath);
	if (!icon.IsValid())
	{
		spdlog::warn("Option: couldn't load window icon from {}", iconPath.string());
		return;
	}

	SDL_Surface* pSurface = SDL_CreateSurfaceFrom(
		icon.width, icon.height, SDL_PIXELFORMAT_RGBA32,
		const_cast<uint8_t*>(icon.pixelsRgba.data()), icon.width * 4);
	if (pSurface == nullptr)
	{
		spdlog::warn("Option: SDL_CreateSurfaceFrom failed: {}", SDL_GetError());
		return;
	}

	SDL_SetWindowIcon(pWindow, pSurface);
	SDL_DestroySurface(pSurface);
}

// A flat, rounded "modern" dark theme, replacing ImGui's stock StyleColorsDark
// (the original MFC dialog's Windows-native fonts/controls have no POSIX
// equivalent worth emulating pixel-for-pixel - this leans into the ImGui
// toolkit's own look instead of imitating the Windows 98-era dialog it
// replaces).
void ApplyModernStyle()
{
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();

	style.WindowRounding    = 8.0f;
	style.ChildRounding     = 6.0f;
	style.FrameRounding     = 5.0f;
	style.PopupRounding     = 6.0f;
	style.ScrollbarRounding = 8.0f;
	style.GrabRounding      = 5.0f;
	style.TabRounding       = 5.0f;

	style.WindowPadding    = ImVec2(16, 16);
	style.FramePadding     = ImVec2(10, 6);
	style.ItemSpacing      = ImVec2(10, 8);
	style.ItemInnerSpacing = ImVec2(8, 6);
	style.IndentSpacing    = 18.0f;
	style.ScrollbarSize    = 14.0f;
	style.GrabMinSize      = 10.0f;

	ImVec4* colors = style.Colors;
	// Slate background with a desaturated blue accent - distinct from the
	// stock ImGui palette every other ImGui tool ships with.
	colors[ImGuiCol_WindowBg]         = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
	colors[ImGuiCol_ChildBg]          = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
	colors[ImGuiCol_PopupBg]          = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
	colors[ImGuiCol_Border]           = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
	colors[ImGuiCol_FrameBg]          = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
	colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
	colors[ImGuiCol_FrameBgActive]    = ImVec4(0.24f, 0.28f, 0.36f, 1.00f);
	colors[ImGuiCol_TitleBg]          = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
	colors[ImGuiCol_TitleBgActive]    = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
	colors[ImGuiCol_CheckMark]        = ImVec4(0.35f, 0.65f, 1.00f, 1.00f);
	colors[ImGuiCol_SliderGrab]       = ImVec4(0.35f, 0.65f, 1.00f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.72f, 1.00f, 1.00f);
	colors[ImGuiCol_Button]           = ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
	colors[ImGuiCol_ButtonHovered]    = ImVec4(0.28f, 0.45f, 0.68f, 1.00f);
	colors[ImGuiCol_ButtonActive]     = ImVec4(0.24f, 0.53f, 0.83f, 1.00f);
	colors[ImGuiCol_Header]           = ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
	colors[ImGuiCol_HeaderHovered]    = ImVec4(0.28f, 0.45f, 0.68f, 1.00f);
	colors[ImGuiCol_HeaderActive]     = ImVec4(0.24f, 0.53f, 0.83f, 1.00f);
	colors[ImGuiCol_Separator]        = ImVec4(0.22f, 0.24f, 0.29f, 1.00f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.35f, 0.65f, 1.00f, 0.60f);
}

struct UiState
{
	option_core::GameOption option;
	std::vector<option_core::Resolution> resolutions;
	int resolutionIndex = 0;
	int colorDepthIndex = 0; // 0 = 16-bit, 1 = 32-bit
	int texLodChr       = 0; // 0 = high, 1 = low
	int texLodShape     = 0;
	int texLodTerrain   = 0;
	bool useShadow      = true;
	int serverVersion   = 0;
};

void SyncUiFromOption(UiState& ui)
{
	ui.texLodChr     = (ui.option.iTexLOD_Chr != 0) ? 1 : 0;
	ui.texLodShape   = (ui.option.iTexLOD_Shape != 0) ? 1 : 0;
	ui.texLodTerrain = (ui.option.iTexLOD_Terrain != 0) ? 1 : 0;
	ui.useShadow     = ui.option.iUseShadow != 0;
	ui.colorDepthIndex = (ui.option.iViewColorDepth == 32) ? 1 : 0;

	ui.resolutionIndex = 0;
	for (size_t i = 0; i < ui.resolutions.size(); ++i)
	{
		if (ui.resolutions[i].Width == static_cast<uint32_t>(ui.option.iViewWidth)
			&& ui.resolutions[i].Height == static_cast<uint32_t>(ui.option.iViewHeight))
		{
			ui.resolutionIndex = static_cast<int>(i);
			break;
		}
	}
}

void SyncOptionFromUi(UiState& ui)
{
	ui.option.iTexLOD_Chr     = ui.texLodChr;
	ui.option.iTexLOD_Shape   = ui.texLodShape;
	ui.option.iTexLOD_Terrain = ui.texLodTerrain;
	ui.option.iUseShadow      = ui.useShadow ? 1 : 0;
	ui.option.iViewColorDepth = (ui.colorDepthIndex == 1) ? 32 : 16;

	if (ui.resolutionIndex >= 0 && ui.resolutionIndex < static_cast<int>(ui.resolutions.size()))
	{
		ui.option.iViewWidth  = static_cast<int>(ui.resolutions[ui.resolutionIndex].Width);
		ui.option.iViewHeight = static_cast<int>(ui.resolutions[ui.resolutionIndex].Height);
	}
}

// Runs the pure Load/Save round-trip against a throwaway Option.ini and
// exits - lets CI (and any headless environment without a working GL
// context) verify the plumbing without a display.
int RunSmokeTest(const fs::path& gameDir)
{
	spdlog::info("Option: --smoke starting (gameDir='{}')", gameDir.string());

	const fs::path iniPath = fs::temp_directory_path() / "openko-option-smoke.ini";
	std::error_code ec;
	fs::remove(iniPath, ec);

	option_core::GameOption written;
	written.iViewWidth  = 1920;
	written.iViewHeight = 1080;
	option_core::SaveOptions(iniPath.string(), written);

	const option_core::GameOption reloaded = option_core::LoadOptions(iniPath.string());
	if (reloaded.iViewWidth != 1920 || reloaded.iViewHeight != 1080)
	{
		spdlog::error("Option: smoke test failed - Option.ini round-trip mismatch");
		return 1;
	}

	const std::vector<option_core::Resolution> resolutions =
		option_core::BuildSupportedResolutions(DetectDisplayResolutions());
	spdlog::info("Option: smoke test detected {} candidate resolution(s)", resolutions.size());

	fs::remove(iniPath, ec);
	spdlog::info("Option: smoke test finished");
	return 0;
}
} // namespace

int main(int argc, char** argv)
{
	int smokeFrames = -1;
	std::string dataDirOverride;
	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--smoke") == 0 && i + 1 < argc)
			smokeFrames = std::atoi(argv[i + 1]);
		else if (std::strcmp(argv[i], "--data") == 0 && i + 1 < argc)
			dataDirOverride = argv[i + 1];
	}

	const fs::path gameDir = ResolveGameDir(dataDirOverride);

	if (smokeFrames >= 0)
		return RunSmokeTest(gameDir);

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		spdlog::error("Option: SDL_Init failed: {}", SDL_GetError());
		return 1;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	SDL_Window* pWindow = SDL_CreateWindow(
		"Knight OnLine - Option", 640, 860, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
	if (pWindow == nullptr)
	{
		spdlog::error("Option: SDL_CreateWindow failed: {}", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	SDL_GLContext glContext = SDL_GL_CreateContext(pWindow);
	if (glContext == nullptr)
	{
		spdlog::error("Option: SDL_GL_CreateContext failed: {}", SDL_GetError());
		SDL_DestroyWindow(pWindow);
		SDL_Quit();
		return 1;
	}
	SDL_GL_MakeCurrent(pWindow, glContext);
	SDL_GL_SetSwapInterval(1);
	ApplyWindowIcon(pWindow);
	SDL_ShowWindow(pWindow);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO().IniFilename = nullptr; // no imgui.ini next to Option.ini
	ApplyModernStyle();
	ImGui_ImplSDL3_InitForOpenGL(pWindow, glContext);
	ImGui_ImplOpenGL3_Init("#version 330");

	UiState ui;
	ui.resolutions   = option_core::BuildSupportedResolutions(DetectDisplayResolutions());
	ui.option        = option_core::LoadOptions((gameDir / "Option.ini").string());
	ui.serverVersion = option_core::ReadServerVersion((gameDir / "Server.Ini").string());
	SyncUiFromOption(ui);

	std::string launchError;

	std::vector<std::string> resolutionLabels;
	resolutionLabels.reserve(ui.resolutions.size());
	for (const option_core::Resolution& r : ui.resolutions)
		resolutionLabels.push_back(std::to_string(r.Width) + " X " + std::to_string(r.Height));

	bool bRunning = true;
	while (bRunning)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			ImGui_ImplSDL3_ProcessEvent(&event);
			if (event.type == SDL_EVENT_QUIT
				|| (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
					&& event.window.windowID == SDL_GetWindowID(pWindow)))
				bRunning = false;
		}

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		int windowW = 0, windowH = 0;
		SDL_GetWindowSize(pWindow, &windowW, &windowH);
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowW), static_cast<float>(windowH)));
		ImGui::Begin("##OptionRoot", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
				| ImGuiWindowFlags_NoCollapse);

		ImGui::SeparatorText("Display");
		if (!resolutionLabels.empty())
		{
			std::vector<const char*> items;
			items.reserve(resolutionLabels.size());
			for (const std::string& s : resolutionLabels)
				items.push_back(s.c_str());
			ImGui::Combo("Resolution", &ui.resolutionIndex, items.data(), static_cast<int>(items.size()));
		}
		const char* colorDepthItems[] = { "16 Bit", "32 Bit" };
		ImGui::Combo("Color depth", &ui.colorDepthIndex, colorDepthItems, 2);
		ImGui::Checkbox("Window mode", &ui.option.bWindowMode);
		ImGui::Checkbox("Use software cursor", &ui.option.bWindowCursor);

		ImGui::SeparatorText("Texture quality");
		ImGui::RadioButton("Character: High", &ui.texLodChr, 0);
		ImGui::SameLine();
		ImGui::RadioButton("Low", &ui.texLodChr, 1);
		ImGui::RadioButton("Object: High", &ui.texLodShape, 0);
		ImGui::SameLine();
		ImGui::RadioButton("Low", &ui.texLodShape, 1);
		ImGui::RadioButton("Terrain: High", &ui.texLodTerrain, 0);
		ImGui::SameLine();
		ImGui::RadioButton("Low", &ui.texLodTerrain, 1);

		ImGui::SeparatorText("Effects");
		ImGui::Checkbox("Shadows", &ui.useShadow);
		ImGui::Checkbox("Show weapon effects", &ui.option.bEffectVisible);
		ImGui::SliderInt("View distance", &ui.option.iViewDist, 256, 512);
		ImGui::SliderInt("Effect count", &ui.option.iEffectCount, 1000, 2000);

		ImGui::SeparatorText("Sound");
		ImGui::Checkbox("Background music", &ui.option.bSoundBgm);
		ImGui::Checkbox("Sound effects", &ui.option.bSoundEffect);
		ImGui::Checkbox("Allow duplicate sounds", &ui.option.bSndDuplicated);
		ImGui::SliderInt("Effect sound distance", &ui.option.iEffectSndDist, 20, 48);

		ImGui::SeparatorText("Version");
		ImGui::InputInt("Server.Ini [Version] Files", &ui.serverVersion);
		if (ImGui::Button("Set version"))
			option_core::WriteServerVersion((gameDir / "Server.Ini").string(), ui.serverVersion);

		ImGui::Spacing();
		ImGui::Separator();
		if (ImGui::Button("OK", ImVec2(100, 0)))
		{
			SyncOptionFromUi(ui);
			option_core::SaveOptions((gameDir / "Option.ini").string(), ui.option);
			bRunning = false;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(100, 0)))
			bRunning = false;
		ImGui::SameLine();
		if (ImGui::Button("Apply and Execute", ImVec2(160, 0)))
		{
			SyncOptionFromUi(ui);
			option_core::SaveOptions((gameDir / "Option.ini").string(), ui.option);
			if (LaunchWarFareAndExit(gameDir))
				bRunning = false;
			else
				launchError = "Couldn't find the KnightOnLine binary next to Option "
							  "(looked in " + GetExecutableDir().string() + " and "
							  + gameDir.string() + "). Your settings were saved.";
		}
		if (!launchError.empty())
		{
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
			ImGui::TextWrapped("%s", launchError.c_str());
			ImGui::PopStyleColor();
		}

		ImGui::End();

		ImGui::Render();
		glViewport(0, 0, windowW, windowH);
		glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(pWindow);
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	SDL_GL_DestroyContext(glContext);
	SDL_DestroyWindow(pWindow);
	SDL_Quit();

	return 0;
}
