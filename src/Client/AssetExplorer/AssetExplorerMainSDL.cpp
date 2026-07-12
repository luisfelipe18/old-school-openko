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

// Engine + RHI: the tool renders previews through the same GL backend the
// client uses, into an offscreen render target it then samples into the UI
// (docs/ASSET_EXPLORER_PLAN.md, M1).
#include <RHIDeviceGL.h>

#include <N3Base/My_3DStruct.h>
#include <N3Base/N3Base.h>

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

	// Preview render target (M1). Sized to the viewport panel; recreated when it
	// changes. viewportSize is measured during the UI pass and consumed at the
	// top of the next frame so the target stays stable within a frame.
	IRHIDevice* rhi = nullptr;
	IRHIRenderTarget* previewRT = nullptr;
	int rtWidth  = 0;
	int rtHeight = 0;
	int viewportW = 0;
	int viewportH = 0;
};

// A single spinning-free RGB triangle in render-target pixel space, proving the
// engine renders into the offscreen target and the tool samples it back. Real
// asset scenes replace this in M3+.
void RenderPreview(ExplorerState& state)
{
	if (state.rhi == nullptr || state.viewportW <= 0 || state.viewportH <= 0)
		return;

	// (Re)create the target when the panel size changes.
	if (state.previewRT == nullptr || state.rtWidth != state.viewportW
		|| state.rtHeight != state.viewportH)
	{
		delete state.previewRT;
		RHIRenderTargetDesc desc;
		desc.width  = static_cast<UINT>(state.viewportW);
		desc.height = static_cast<UINT>(state.viewportH);
		desc.depth  = true;
		state.previewRT = state.rhi->CreateRenderTarget(desc);
		state.rtWidth   = state.viewportW;
		state.rtHeight  = state.viewportH;
	}
	if (state.previewRT == nullptr)
		return;

	struct VtxRHW
	{
		float x, y, z, rhw;
		uint32_t color; // 0xAARRGGBB (D3D DIFFUSE)
	};
	const float w = static_cast<float>(state.rtWidth);
	const float h = static_cast<float>(state.rtHeight);
	const float cx = w * 0.5f;
	const float cy = h * 0.5f;
	const float r  = 0.35f * ((w < h) ? w : h);
	const VtxRHW verts[3] = {
		{cx,          cy - r, 0.0f, 1.0f, 0xFFE24A4A},
		{cx + r,      cy + r, 0.0f, 1.0f, 0xFF4AE26A},
		{cx - r,      cy + r, 0.0f, 1.0f, 0xFF4A78E2},
	};

	state.rhi->BeginRenderTarget(state.previewRT);
	state.rhi->Clear(D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF14161B, 1.0f, 0);
	state.rhi->SetRenderState(D3DRS_LIGHTING, FALSE);
	state.rhi->SetRenderState(D3DRS_ZENABLE, FALSE);
	state.rhi->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	state.rhi->SetTexture(0, nullptr);
	state.rhi->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	state.rhi->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	state.rhi->SetFVF(FVF_TRANSFORMEDCOLOR);
	state.rhi->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, verts, sizeof(VtxRHW));
	state.rhi->EndRenderTarget();
}

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

void DrawViewport(ExplorerState& state, const ImVec2& size)
{
	if (!ImGui::BeginChild("##viewport", size, ImGuiChildFlags_Borders))
	{
		ImGui::EndChild();
		return;
	}
	const ImVec2 avail = ImGui::GetContentRegionAvail();

	// Record the panel size for next frame's render (kept a frame behind so the
	// target isn't resized mid-frame while its texture is referenced here).
	state.viewportW = static_cast<int>(avail.x > 1.0f ? avail.x : 1.0f);
	state.viewportH = static_cast<int>(avail.y > 1.0f ? avail.y : 1.0f);

	if (state.previewRT != nullptr && state.previewRT->ColorHandle() != nullptr)
	{
		// GL renders bottom-up into the target texture; flip V so it displays
		// upright under ImGui's top-left UV convention.
		const ImTextureID tex = reinterpret_cast<ImTextureID>(state.previewRT->ColorHandle());
		ImGui::Image(tex, avail, ImVec2(0, 1), ImVec2(1, 0));
	}
	else
	{
		const char* msg = "Preview viewport (no render target)";
		const ImVec2 sz = ImGui::CalcTextSize(msg);
		ImGui::SetCursorPos(ImVec2((avail.x - sz.x) * 0.5f, avail.y * 0.5f));
		ImGui::TextDisabled("%s", msg);
	}
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

// Offscreen render smoke (docs/ASSET_EXPLORER_PLAN.md, M1): brings up the GL
// backend, renders the test triangle into a render target and reads back the
// center pixel, asserting it is the drawn geometry rather than the clear color.
// Needs a GL context (run under Xvfb in CI). Returns 0 on success.
int RunRenderSmokeTest()
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		spdlog::error("AssetExplorer: SDL_Init failed: {}", SDL_GetError());
		return 1;
	}
	RHIDeviceGL::SetGLWindowAttributes();
	SDL_Window* pWindow = SDL_CreateWindow("AssetExplorer smoke", 320, 240,
		SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
	if (pWindow == nullptr)
	{
		spdlog::error("AssetExplorer: SDL_CreateWindow failed: {}", SDL_GetError());
		SDL_Quit();
		return 1;
	}
	auto* pRHI = new RHIDeviceGL(pWindow, /*bVSync=*/false);
	int rc = 1;
	if (pRHI->IsValid())
	{
		CN3Base::RHIDeviceSet(pRHI);
		ExplorerState state;
		state.rhi = pRHI;
		state.viewportW = 256;
		state.viewportH = 256;
		RenderPreview(state);

		uint8_t rgba[4] = {};
		const bool read = state.previewRT != nullptr
			&& pRHI->ReadRenderTargetPixel(state.previewRT, 128, 128, rgba);
		// The clear color is 0x14161B (dark); the triangle covers the center, so
		// a correct render reads back something clearly brighter.
		const int lum = rgba[0] + rgba[1] + rgba[2];
		std::printf("AssetExplorer render smoke: center rgba=(%d,%d,%d,%d) read=%d\n",
			rgba[0], rgba[1], rgba[2], rgba[3], read ? 1 : 0);
		rc = (read && lum > 96) ? 0 : 3;

		delete state.previewRT;
		CN3Base::RHIDeviceSet(nullptr);
	}
	delete pRHI;
	SDL_DestroyWindow(pWindow);
	SDL_Quit();
	return rc;
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
	bool renderSmoke = false;
	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--smoke") == 0 && i + 1 < argc)
			smokeDir = argv[++i];
		else if (std::strcmp(argv[i], "--smoke-render") == 0)
			renderSmoke = true;
		else if (std::strcmp(argv[i], "--data") == 0 && i + 1 < argc)
			dataOverride = argv[++i];
		else if (argv[i][0] != '-')
			dataOverride = argv[i];
	}

	if (renderSmoke)
		return RunRenderSmokeTest();
	if (!smokeDir.empty())
		return RunSmokeTest(smokeDir);

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		spdlog::error("AssetExplorer: SDL_Init failed: {}", SDL_GetError());
		return 1;
	}

	// The engine's GL backend owns the context (matches the WarFare client). It
	// sets the GL window attributes, so do that before creating the window.
	RHIDeviceGL::SetGLWindowAttributes();

	SDL_Window* pWindow = SDL_CreateWindow("Knight OnLine - Asset Explorer", 1280, 800,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
	if (pWindow == nullptr)
	{
		spdlog::error("AssetExplorer: SDL_CreateWindow failed: {}", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	auto* pRHI = new RHIDeviceGL(pWindow, /*bVSync=*/true);
	if (!pRHI->IsValid())
	{
		spdlog::error("AssetExplorer: GL backend init failed");
		delete pRHI;
		SDL_DestroyWindow(pWindow);
		SDL_Quit();
		return 1;
	}
	CN3Base::RHIDeviceSet(pRHI);
	SDL_GLContext glContext = static_cast<SDL_GLContext>(pRHI->GLContext());

	ApplyWindowIcon(pWindow);
	SDL_ShowWindow(pWindow);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO().IniFilename = nullptr;
	ApplyModernStyle();
	// Share the engine's GL context: ImGui records draw data and replays it into
	// the window framebuffer after the engine has rendered previews into RTs.
	ImGui_ImplSDL3_InitForOpenGL(pWindow, glContext);
	ImGui_ImplOpenGL3_Init("#version 330");

	ExplorerState state;
	state.rhi = pRHI;
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

		// Render this frame's preview into the offscreen target using the panel
		// size measured last frame, so the target texture is ready before the UI
		// references it below.
		RenderPreview(state);

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
		DrawViewport(state, ImVec2(-(inspectorW + ImGui::GetStyle().ItemSpacing.x), bodyH));
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

	// Delete GL resources while the context is still current, then tear down the
	// backend (its destructor destroys the GL context).
	delete state.previewRT;
	CN3Base::RHIDeviceSet(nullptr);
	delete pRHI;

	SDL_DestroyWindow(pWindow);
	SDL_Quit();

	return 0;
}
