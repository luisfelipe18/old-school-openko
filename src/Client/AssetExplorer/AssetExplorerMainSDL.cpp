// POSIX entry point for the Asset Explorer tool
// (docs/ASSET_EXPLORER_PLAN.md, M0). A modern Dear ImGui app over the SDL3 +
// OpenGL stack the other client-tool ports use. This milestone (M0) delivers
// the shell: it discovers a game-data directory, indexes every asset by type,
// and presents a searchable/filterable three-pane layout (asset list, preview
// viewport placeholder, inspector). 3D/2D preview lands in M1+.
//
// A --smoke <dir> flag indexes a directory headlessly and prints the per-type
// counts for CI, without opening a window.

#include "AssetIndex.h"
#include "AssetType.h"

#include <Platform/GameDataDir.h>
#include <Platform/IconDecoder.h>
#include <Platform/PlatformPaths.h>

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

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
using namespace assetexplorer;

namespace
{

// The chip categories shown in the toolbar filter row, in display order.
constexpr std::array<AssetCategory, 5> kFilterChips{
	AssetCategory::Texture, AssetCategory::Model, AssetCategory::Character,
	AssetCategory::Effect, AssetCategory::Map};

// Same flat, rounded dark theme as the Option/Launcher/KscViewer ports so the
// tools look like a matched set (see OptionMainSDL.cpp for the rationale).
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

	ImVec4* colors = style.Colors;
	colors[ImGuiCol_WindowBg]       = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
	colors[ImGuiCol_ChildBg]        = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
	colors[ImGuiCol_PopupBg]        = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
	colors[ImGuiCol_Border]         = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
	colors[ImGuiCol_FrameBg]        = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
	colors[ImGuiCol_FrameBgActive]  = ImVec4(0.24f, 0.28f, 0.36f, 1.00f);
	colors[ImGuiCol_TitleBg]        = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
	colors[ImGuiCol_TitleBgActive]  = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
	colors[ImGuiCol_Button]         = ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
	colors[ImGuiCol_ButtonHovered]  = ImVec4(0.28f, 0.45f, 0.68f, 1.00f);
	colors[ImGuiCol_ButtonActive]   = ImVec4(0.24f, 0.53f, 0.83f, 1.00f);
	colors[ImGuiCol_Header]         = ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
	colors[ImGuiCol_HeaderHovered]  = ImVec4(0.28f, 0.45f, 0.68f, 1.00f);
	colors[ImGuiCol_HeaderActive]   = ImVec4(0.24f, 0.53f, 0.83f, 1.00f);
	colors[ImGuiCol_Separator]      = ImVec4(0.22f, 0.24f, 0.29f, 1.00f);
}

void ApplyWindowIcon(SDL_Window* pWindow)
{
	const fs::path iconPath = GetExecutableDir() / "AssetExplorer.ico";
	const DecodedIcon icon  = LoadIconFromFile(iconPath);
	if (!icon.IsValid())
		return; // best-effort, like the other tools

	SDL_Surface* pSurface = SDL_CreateSurfaceFrom(icon.width, icon.height,
		SDL_PIXELFORMAT_RGBA32, const_cast<uint8_t*>(icon.pixelsRgba.data()), icon.width * 4);
	if (pSurface != nullptr)
	{
		SDL_SetWindowIcon(pWindow, pSurface);
		SDL_DestroySurface(pSurface);
	}
}

std::string HumanSize(std::uintmax_t bytes)
{
	const char* units[] = {"B", "KB", "MB", "GB"};
	double v = static_cast<double>(bytes);
	int u = 0;
	while (v >= 1024.0 && u < 3)
	{
		v /= 1024.0;
		++u;
	}
	char buf[32];
	std::snprintf(buf, sizeof(buf), (u == 0) ? "%.0f %s" : "%.1f %s", v, units[u]);
	return buf;
}

// All UI/session state for the explorer.
struct ExplorerState
{
	AssetIndex index;
	fs::path dataDir;
	std::string status;

	char search[256] = {};
	unsigned categoryMask = 0; // 0 == all
	int selected = -1;         // index into index.Entries(), or -1
};

void RescanDataDir(ExplorerState& state, const fs::path& dir)
{
	state.dataDir = dir;
	state.selected = -1;
	const std::size_t n = state.index.Scan(dir);
	state.status = n == 0
		? "No assets found under " + dir.string()
		: std::to_string(n) + " assets indexed under " + dir.string();
}

void DrawToolbar(ExplorerState& state)
{
	if (ImGui::Button("Rescan"))
		RescanDataDir(state, state.dataDir);
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();

	ImGui::SetNextItemWidth(280);
	ImGui::InputTextWithHint("##search", "Search assets...", state.search, sizeof(state.search));
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();

	// Category filter chips: click to toggle the category's bit in the mask.
	const auto counts = state.index.CountsByCategory();
	for (AssetCategory cat : kFilterChips)
	{
		const unsigned bit = CategoryBit(cat);
		const bool on = (state.categoryMask & bit) != 0;
		const std::size_t count = counts[static_cast<std::size_t>(cat)];

		char label[64];
		std::snprintf(label, sizeof(label), "%s (%zu)", AssetCategoryLabel(cat), count);

		if (on)
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		if (ImGui::Button(label))
			state.categoryMask ^= bit;
		if (on)
			ImGui::PopStyleColor();
		ImGui::SameLine();
	}
	if (state.categoryMask != 0)
	{
		if (ImGui::Button("Clear"))
			state.categoryMask = 0;
	}
	else
	{
		ImGui::TextDisabled("all");
	}
}

void DrawAssetList(ExplorerState& state, const ImVec2& size)
{
	if (!ImGui::BeginChild("##assetlist", size, ImGuiChildFlags_Borders))
	{
		ImGui::EndChild();
		return;
	}

	const std::vector<std::size_t> shown = state.index.Filter(state.search, state.categoryMask);
	ImGui::Text("%zu / %zu assets", shown.size(), state.index.Entries().size());
	ImGui::Separator();

	// A clipper keeps a multi-thousand-entry list smooth.
	ImGuiListClipper clipper;
	clipper.Begin(static_cast<int>(shown.size()));
	while (clipper.Step())
	{
		for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
		{
			const std::size_t entryIdx = shown[static_cast<std::size_t>(row)];
			const AssetEntry& e = state.index.Entries()[entryIdx];

			char label[512];
			std::snprintf(label, sizeof(label), "[%s] %s",
				AssetCategoryLabel(e.category), e.relativePath.c_str());

			const bool selected = (state.selected == static_cast<int>(entryIdx));
			if (ImGui::Selectable(label, selected))
				state.selected = static_cast<int>(entryIdx);
		}
	}
	ImGui::EndChild();
}

void DrawViewport(const ImVec2& size)
{
	if (!ImGui::BeginChild("##viewport", size, ImGuiChildFlags_Borders))
	{
		ImGui::EndChild();
		return;
	}
	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const char* msg = "Preview viewport";
	const char* sub = "3D/2D preview arrives with the RHI render target (M1+).";
	const ImVec2 sz1 = ImGui::CalcTextSize(msg);
	const ImVec2 sz2 = ImGui::CalcTextSize(sub);
	ImGui::SetCursorPos(ImVec2((avail.x - sz1.x) * 0.5f, avail.y * 0.5f - sz1.y));
	ImGui::TextDisabled("%s", msg);
	ImGui::SetCursorPosX((avail.x - sz2.x) * 0.5f);
	ImGui::TextDisabled("%s", sub);
	ImGui::EndChild();
}

void DrawInspector(ExplorerState& state, const ImVec2& size)
{
	if (!ImGui::BeginChild("##inspector", size, ImGuiChildFlags_Borders))
	{
		ImGui::EndChild();
		return;
	}
	ImGui::TextUnformatted("Inspector");
	ImGui::Separator();

	if (state.selected < 0
		|| state.selected >= static_cast<int>(state.index.Entries().size()))
	{
		ImGui::TextDisabled("Select an asset to inspect.");
		ImGui::EndChild();
		return;
	}

	const AssetEntry& e = state.index.Entries()[static_cast<std::size_t>(state.selected)];
	ImGui::Text("Name");
	ImGui::TextWrapped("%s", e.fileName.c_str());
	ImGui::Spacing();
	ImGui::Text("Path");
	ImGui::TextWrapped("%s", e.relativePath.c_str());
	ImGui::Spacing();
	ImGui::Text("Type");
	ImGui::TextDisabled("%s", AssetTypeName(e.type));
	ImGui::Spacing();
	ImGui::Text("Size");
	ImGui::TextDisabled("%s", HumanSize(e.sizeBytes).c_str());
	ImGui::EndChild();
}

int RunSmokeTest(const std::string& dir)
{
	AssetIndex index;
	const std::size_t n = index.Scan(dir);
	const auto counts = index.CountsByCategory();
	std::printf("AssetExplorer smoke: %zu assets under %s\n", n, dir.c_str());
	for (AssetCategory cat : kFilterChips)
		std::printf("  %-6s %zu\n", AssetCategoryLabel(cat),
			counts[static_cast<std::size_t>(cat)]);
	return n > 0 ? 0 : 2;
}

} // namespace

int main(int argc, char** argv)
{
	std::string smokeDir;
	std::string dataOverride;
	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--smoke") == 0 && i + 1 < argc)
			smokeDir = argv[++i];
		else if (std::strcmp(argv[i], "--data") == 0 && i + 1 < argc)
			dataOverride = argv[++i];
		else if (argv[i][0] != '-')
			dataOverride = argv[i];
	}

	if (!smokeDir.empty())
		return RunSmokeTest(smokeDir);

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		spdlog::error("AssetExplorer: SDL_Init failed: {}", SDL_GetError());
		return 1;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	SDL_Window* pWindow = SDL_CreateWindow("Knight OnLine - Asset Explorer", 1280, 800,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
	if (pWindow == nullptr)
	{
		spdlog::error("AssetExplorer: SDL_CreateWindow failed: {}", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	SDL_GLContext glContext = SDL_GL_CreateContext(pWindow);
	if (glContext == nullptr)
	{
		spdlog::error("AssetExplorer: SDL_GL_CreateContext failed: {}", SDL_GetError());
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
	ImGui::GetIO().IniFilename = nullptr;
	ApplyModernStyle();
	ImGui_ImplSDL3_InitForOpenGL(pWindow, glContext);
	ImGui_ImplOpenGL3_Init("#version 330");

	ExplorerState state;
	{
		const fs::path dir = FindGameDataDir(dataOverride);
		if (dir.empty())
		{
			std::error_code ec;
			state.dataDir = fs::current_path(ec);
			state.status = "No game-data directory found; pass --data <path>. "
						   "Showing current directory.";
			RescanDataDir(state, state.dataDir);
		}
		else
		{
			RescanDataDir(state, dir);
		}
	}

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
		ImGui::Begin("##AssetExplorerRoot", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
				| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

		DrawToolbar(state);
		ImGui::Separator();

		// Body: list (fixed) | viewport (fills middle) | inspector (fixed),
		// all sharing one row and leaving space for the status bar below.
		const float listW      = 320.0f;
		const float inspectorW  = 300.0f;
		const float bodyH       = -ImGui::GetFrameHeightWithSpacing();
		DrawAssetList(state, ImVec2(listW, bodyH));
		ImGui::SameLine();
		DrawViewport(ImVec2(-(inspectorW + ImGui::GetStyle().ItemSpacing.x), bodyH));
		ImGui::SameLine();
		DrawInspector(state, ImVec2(0, bodyH));

		// Status bar.
		ImGui::Separator();
		ImGui::TextDisabled("%s", state.status.empty() ? "Ready" : state.status.c_str());

		ImGui::End();

		ImGui::Render();
		glViewport(0, 0, windowW, windowH);
		glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
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
