// POSIX entry point for the KscViewer tool (docs/PORT_POSIX_PLAN.md, F10):
// the original is an MFC SDI doc/view app; MFC is Windows-only, so this
// reimplements it on Dear ImGui over the SDL3 + OpenGL 3.3 core stack the
// other client-tool ports (Option, Launcher) use. It opens a .ksc (encrypted
// JPEG splash/loading image) or a plain .jpg, shows it, and exports the
// decrypted JPEG - the exact feature set of the Windows tool.
//
// A --smoke <path> flag decrypts+decodes headlessly for CI.

#include "KscViewerCore.h"

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

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace
{
// Same flat, rounded dark theme as the Option/Launcher ports so the tools
// look like a matched set (see OptionMainSDL.cpp for the rationale).
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
	colors[ImGuiCol_WindowBg]         = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
	colors[ImGuiCol_ChildBg]          = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
	colors[ImGuiCol_PopupBg]          = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
	colors[ImGuiCol_Border]           = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
	colors[ImGuiCol_FrameBg]          = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
	colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
	colors[ImGuiCol_FrameBgActive]    = ImVec4(0.24f, 0.28f, 0.36f, 1.00f);
	colors[ImGuiCol_TitleBg]          = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
	colors[ImGuiCol_TitleBgActive]    = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
	colors[ImGuiCol_Button]           = ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
	colors[ImGuiCol_ButtonHovered]    = ImVec4(0.28f, 0.45f, 0.68f, 1.00f);
	colors[ImGuiCol_ButtonActive]     = ImVec4(0.24f, 0.53f, 0.83f, 1.00f);
	colors[ImGuiCol_Header]           = ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
	colors[ImGuiCol_HeaderHovered]    = ImVec4(0.28f, 0.45f, 0.68f, 1.00f);
	colors[ImGuiCol_HeaderActive]     = ImVec4(0.24f, 0.53f, 0.83f, 1.00f);
	colors[ImGuiCol_Separator]        = ImVec4(0.22f, 0.24f, 0.29f, 1.00f);
}

void ApplyWindowIcon(SDL_Window* pWindow)
{
	const fs::path iconPath = GetExecutableDir() / "KscViewer.ico";
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

// Holds the currently displayed image plus its GL texture.
struct ViewerState
{
	fs::path currentDir;
	fs::path loadedPath;
	std::string status;
	ksc_core::DecodedImage image;
	GLuint texture   = 0;
	int texWidth     = 0;
	int texHeight    = 0;
	char exportName[512] = {};
};

void UploadTexture(ViewerState& state)
{
	if (state.texture == 0)
		glGenTextures(1, &state.texture);

	glBindTexture(GL_TEXTURE_2D, state.texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	// JpegFileToRGB hands back top-down RGB, which maps directly to a GL
	// texture sampled with ImGui's default (0,0)-top-left UVs.
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, state.image.width, state.image.height, 0, GL_RGB,
		GL_UNSIGNED_BYTE, state.image.rgb.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	state.texWidth  = state.image.width;
	state.texHeight = state.image.height;
}

void LoadPath(ViewerState& state, const fs::path& path)
{
	ksc_core::DecodedImage image = ksc_core::LoadImage(path);
	if (!image.IsValid())
	{
		state.status = "Failed to load '" + path.filename().string()
					   + "' (not a valid .ksc/.jpg, or unsupported)";
		return;
	}

	state.image      = std::move(image);
	state.loadedPath = path;
	UploadTexture(state);

	// Default export name: same stem with a .jpg extension.
	const std::string suggested = path.stem().string() + ".jpg";
	std::snprintf(state.exportName, sizeof(state.exportName), "%s", suggested.c_str());

	state.status = "Loaded " + path.filename().string() + " (" + std::to_string(state.image.width)
				   + "x" + std::to_string(state.image.height) + ")";
}

// Lists the current directory's subdirectories and .ksc/.jpg files, sorted,
// as a click-to-navigate / click-to-load browser (no native file dialog on
// the SDL/ImGui stack).
void DrawBrowser(ViewerState& state)
{
	ImGui::TextUnformatted("Folder:");
	ImGui::SameLine();
	ImGui::TextWrapped("%s", state.currentDir.string().c_str());

	if (ImGui::BeginChild("##browser", ImVec2(280, 0), ImGuiChildFlags_Borders))
	{
		std::error_code ec;

		if (state.currentDir.has_parent_path()
			&& ImGui::Selectable(".. (up)", false))
		{
			state.currentDir = state.currentDir.parent_path();
		}

		std::vector<fs::path> dirs;
		std::vector<fs::path> files;
		for (const fs::directory_entry& entry :
			fs::directory_iterator(state.currentDir, fs::directory_options::skip_permission_denied, ec))
		{
			if (entry.is_directory(ec))
			{
				dirs.push_back(entry.path());
			}
			else
			{
				std::string ext = entry.path().extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (ext == ".ksc" || ext == ".jpg" || ext == ".jpeg")
					files.push_back(entry.path());
			}
		}
		std::sort(dirs.begin(), dirs.end());
		std::sort(files.begin(), files.end());

		for (const fs::path& dir : dirs)
		{
			const std::string label = "[dir] " + dir.filename().string();
			if (ImGui::Selectable(label.c_str(), false))
				state.currentDir = dir;
		}
		for (const fs::path& file : files)
		{
			const bool selected = (file == state.loadedPath);
			if (ImGui::Selectable(file.filename().string().c_str(), selected))
				LoadPath(state, file);
		}
	}
	ImGui::EndChild();
}

void DrawImagePanel(ViewerState& state)
{
	if (ImGui::BeginChild("##imagepanel", ImVec2(0, 0), ImGuiChildFlags_Borders))
	{
		if (state.texture != 0 && state.image.IsValid())
		{
			// Fit the image into the available area, preserving aspect.
			const ImVec2 avail = ImGui::GetContentRegionAvail();
			const float imgW   = static_cast<float>(state.texWidth);
			const float imgH   = static_cast<float>(state.texHeight);
			float scale        = std::min(avail.x / imgW, avail.y / imgH);
			if (scale > 1.0f)
				scale = 1.0f; // don't upscale past native size
			if (scale <= 0.0f)
				scale = 1.0f;
			const ImVec2 drawSize(imgW * scale, imgH * scale);

			// Center it.
			const ImVec2 cursor = ImGui::GetCursorPos();
			ImGui::SetCursorPos(ImVec2(cursor.x + (avail.x - drawSize.x) * 0.5f,
				cursor.y + (avail.y - drawSize.y) * 0.5f));
			ImGui::Image(static_cast<ImTextureID>(state.texture), drawSize);
		}
		else
		{
			ImGui::TextDisabled("Select a .ksc or .jpg file on the left to preview it.");
		}
	}
	ImGui::EndChild();
}

int RunSmokeTest(const fs::path& path)
{
	spdlog::info("KscViewer: --smoke decoding '{}'", path.string());
	const ksc_core::DecodedImage image = ksc_core::LoadImage(path);
	if (!image.IsValid())
	{
		spdlog::error("KscViewer: smoke test failed - could not decode '{}'", path.string());
		return 1;
	}
	spdlog::info("KscViewer: smoke test decoded {}x{} OK", image.width, image.height);
	return 0;
}
} // namespace

int main(int argc, char** argv)
{
	std::string smokePath;
	std::string openPath;
	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--smoke") == 0 && i + 1 < argc)
			smokePath = argv[++i];
		else if (std::strcmp(argv[i], "--file") == 0 && i + 1 < argc)
			openPath = argv[++i];
		else if (argv[i][0] != '-')
			openPath = argv[i]; // bare path argument
	}

	if (!smokePath.empty())
		return RunSmokeTest(smokePath);

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		spdlog::error("KscViewer: SDL_Init failed: {}", SDL_GetError());
		return 1;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	SDL_Window* pWindow = SDL_CreateWindow(
		"Knight OnLine - KSC Viewer", 960, 640, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
	if (pWindow == nullptr)
	{
		spdlog::error("KscViewer: SDL_CreateWindow failed: {}", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	SDL_GLContext glContext = SDL_GL_CreateContext(pWindow);
	if (glContext == nullptr)
	{
		spdlog::error("KscViewer: SDL_GL_CreateContext failed: {}", SDL_GetError());
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

	ViewerState state;
	std::error_code ec;
	state.currentDir = fs::current_path(ec);
	if (ec)
		state.currentDir = fs::path(".");

	if (!openPath.empty())
	{
		const fs::path p = fs::absolute(openPath, ec);
		LoadPath(state, ec ? fs::path(openPath) : p);
		if (state.loadedPath.has_parent_path())
			state.currentDir = state.loadedPath.parent_path();
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
		ImGui::Begin("##KscViewerRoot", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
				| ImGuiWindowFlags_NoCollapse);

		// Top bar: export controls.
		ImGui::BeginDisabled(!state.image.IsValid());
		ImGui::SetNextItemWidth(280);
		ImGui::InputText("##exportname", state.exportName, sizeof(state.exportName));
		ImGui::SameLine();
		if (ImGui::Button("Export JPEG"))
		{
			fs::path dst = state.loadedPath.has_parent_path()
							   ? state.loadedPath.parent_path() / state.exportName
							   : fs::path(state.exportName);
			if (ksc_core::ExportJpg(state.loadedPath, dst))
				state.status = "Exported " + dst.string();
			else
				state.status = "Export failed for " + dst.string();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();
		ImGui::TextUnformatted(state.status.empty() ? "Ready" : state.status.c_str());

		ImGui::Separator();

		// Body: browser on the left, image on the right.
		DrawBrowser(state);
		ImGui::SameLine();
		DrawImagePanel(state);

		ImGui::End();

		ImGui::Render();
		glViewport(0, 0, windowW, windowH);
		glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(pWindow);
	}

	if (state.texture != 0)
		glDeleteTextures(1, &state.texture);

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	SDL_GL_DestroyContext(glContext);
	SDL_DestroyWindow(pWindow);
	SDL_Quit();

	return 0;
}
