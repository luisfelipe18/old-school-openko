// POSIX entry point for the Launcher tool (docs/PORT_POSIX_PLAN.md, F10):
// the original is an MFC dialog (LauncherDlg.cpp) that reads its install
// version from the Windows registry, downloads patches over FTP, and
// extracts them with a Windows-only ZipArchive - none of which has a POSIX
// equivalent yet. This ports the part of the flow that's actually
// functional against the real protocol: connect to the same server list
// WarFare's login scene uses, ask VersionManager for its version
// (LauncherCore.h - byte-identical framing/packets to the original), and
// launch the game directly when versions match. When the server reports a
// newer version, the patch file list is fetched and shown (so the user
// knows what changed) but not auto-downloaded yet - see LauncherCore.h for
// why the login-via-Launcher path (LOGIN_REQ/SERVER_LIST) isn't
// reimplemented: it's dead code against the current protocol even in the
// original Windows binary.
//
// The original dialog's only real UI is a progress bar + status line
// (IDD_LAUNCHER_DIALOG in Launcher.rc is a 279x60 strip) - the background/
// button bitmaps in res/ (Bkg.bmp, Btn_*.bmp, Edit_*.bmp) are compiled in
// but never loaded by any code path (no LoadBitmap call references their
// IDB_* resource IDs), so this reproduces the dialog that actually ran
// rather than inventing a skin that never shipped.

#include "LauncherCore.h"

#include <Platform/GameDataDir.h>
#include <Platform/IconDecoder.h>
#include <Platform/PlatformPaths.h>
#include <Platform/ProcessLaunch.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <spdlog/spdlog.h>

#include <SDL3/SDL.h>

#include <asio.hpp>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace
{
// Must always match GameDef.h's CURRENT_VERSION - kept as a separate
// constant rather than linking WarFare.Core (which would drag in the whole
// engine/RHI for a version number), same tradeoff the original Launcher's
// own Define.h/PacketDef.h made by not sharing WarFare's headers either.
constexpr int CLIENT_VERSION = 1298;

constexpr int VERSIONMANAGER_PORT = 15100; // SOCKET_PORT_LOGIN / CONNECT_PORT

enum class LauncherState
{
	Connecting,
	UpToDate,
	NeedsUpdate,
	Error,
};

struct SharedState
{
	std::mutex mutex;
	LauncherState state = LauncherState::Connecting;
	std::string statusText = "Connecting...";
	int serverVersion      = 0;
	launcher_core::DownloadInfoResponse downloadInfo;
};

void SetStatus(SharedState& shared, LauncherState state, std::string text)
{
	std::lock_guard<std::mutex> lock(shared.mutex);
	shared.state      = state;
	shared.statusText = std::move(text);
}

// Blocking read loop: accumulates socket bytes until one complete frame is
// extracted (or the connection closes / a hard error occurs).
bool ReadOneFrame(asio::ip::tcp::socket& socket, std::vector<uint8_t>& buffer,
	std::vector<uint8_t>& outPayload, std::string& error)
{
	while (true)
	{
		if (launcher_core::TryExtractFrame(buffer, outPayload))
			return true;

		std::array<uint8_t, 512> chunk {};
		asio::error_code ec;
		const size_t n = socket.read_some(asio::buffer(chunk), ec);
		if (ec)
		{
			error = ec.message();
			return false;
		}
		buffer.insert(buffer.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n));

		if (buffer.size() > 64 * 1024) // guard against a misbehaving/hostile peer
		{
			error = "response too large";
			return false;
		}
	}
}

void NetworkThreadMain(SharedState& shared, fs::path gameDir)
{
	const std::vector<std::string> ips = launcher_core::ReadServerIpList((gameDir / "Server.Ini").string());
	if (ips.empty())
	{
		SetStatus(shared, LauncherState::Error, "No servers listed in Server.Ini");
		return;
	}

	const std::string& ip = ips[static_cast<size_t>(std::rand()) % ips.size()];
	spdlog::info("Launcher: connecting to {}:{}", ip, VERSIONMANAGER_PORT);

	asio::io_context io;
	asio::ip::tcp::socket socket(io);
	asio::error_code ec;

	asio::ip::tcp::resolver resolver(io);
	const auto endpoints = resolver.resolve(ip, std::to_string(VERSIONMANAGER_PORT), ec);
	if (ec)
	{
		SetStatus(shared, LauncherState::Error, "Couldn't resolve " + ip + ": " + ec.message());
		return;
	}

	asio::connect(socket, endpoints, ec);
	if (ec)
	{
		SetStatus(shared, LauncherState::Error, "Couldn't connect to " + ip + ": " + ec.message());
		return;
	}

	SetStatus(shared, LauncherState::Connecting, "Checking version...");

	const std::vector<uint8_t> versionReqFrame =
		launcher_core::FrameMessage(launcher_core::BuildVersionRequest(CLIENT_VERSION));
	asio::write(socket, asio::buffer(versionReqFrame), ec);
	if (ec)
	{
		SetStatus(shared, LauncherState::Error, "Send failed: " + ec.message());
		return;
	}

	std::vector<uint8_t> recvBuffer;
	std::vector<uint8_t> payload;
	std::string readError;
	if (!ReadOneFrame(socket, recvBuffer, payload, readError))
	{
		SetStatus(shared, LauncherState::Error, "No response from server: " + readError);
		return;
	}

	launcher_core::VersionResponse versionResp;
	if (!launcher_core::ParseVersionResponse(payload, versionResp))
	{
		SetStatus(shared, LauncherState::Error, "Malformed version response");
		return;
	}

	spdlog::info("Launcher: server reports version {} (client is {})", versionResp.serverVersion,
		CLIENT_VERSION);

	{
		std::lock_guard<std::mutex> lock(shared.mutex);
		shared.serverVersion = versionResp.serverVersion;
	}

	if (versionResp.serverVersion == CLIENT_VERSION)
	{
		SetStatus(shared, LauncherState::UpToDate, "Up to date - starting game...");
		return;
	}

	if (versionResp.serverVersion < CLIENT_VERSION)
	{
		SetStatus(shared, LauncherState::Error, "Client version is newer than the server's - check Server.Ini");
		return;
	}

	// Server is ahead of us: ask what changed. Downloading/applying the
	// patch (FTP + ZIP extraction) isn't ported yet (docs/PORT_POSIX_PLAN.md
	// F10) - report what's available instead of pretending to update.
	const std::vector<uint8_t> downloadReqFrame =
		launcher_core::FrameMessage(launcher_core::BuildDownloadInfoRequest(CLIENT_VERSION));
	asio::write(socket, asio::buffer(downloadReqFrame), ec);
	if (ec)
	{
		SetStatus(shared, LauncherState::Error, "Send failed: " + ec.message());
		return;
	}

	recvBuffer.clear();
	if (!ReadOneFrame(socket, recvBuffer, payload, readError))
	{
		SetStatus(shared, LauncherState::Error, "No download info from server: " + readError);
		return;
	}

	launcher_core::DownloadInfoResponse downloadInfo;
	if (!launcher_core::ParseDownloadInfoResponse(payload, downloadInfo))
	{
		SetStatus(shared, LauncherState::Error, "Malformed download info response");
		return;
	}

	{
		std::lock_guard<std::mutex> lock(shared.mutex);
		shared.downloadInfo = downloadInfo;
		shared.state        = LauncherState::NeedsUpdate;
		shared.statusText   = "Update available (" + std::to_string(downloadInfo.fileNames.size())
							 + " file(s)) - patch download isn't supported on this platform yet";
	}
}

// Returns false (leaving the Launcher window open with an error instead of
// handing off silently) when the binary can't be found - e.g. WarFare ships
// as a KnightOnLine.app bundle on macOS, which FindSiblingExecutable
// accounts for.
bool LaunchWarFareAndExit(const fs::path& gameDir)
{
	const fs::path candidate = platform_launch::FindSiblingExecutable(
		{ GetExecutableDir(), gameDir }, "KnightOnLine");

	if (candidate.empty())
	{
		spdlog::warn("Launcher: couldn't find the KnightOnLine binary next to this tool.");
		return false;
	}

	spdlog::info("Launcher: launching {}", candidate.string());
	// Forward the resolved game data dir explicitly (StartGame() on Windows
	// re-passes its own command line the same way) - but only when it's a
	// real one; see Option's identical comment for why the CWD fallback
	// isn't forwarded.
	platform_launch::LaunchDetached(candidate, gameDir);
	return true;
}

// Decodes Launcher.ico (staged next to the binary by CMake) and applies it
// as the SDL window icon. Best-effort: a missing/undecodable file just
// leaves the window manager's default icon.
void ApplyWindowIcon(SDL_Window* pWindow)
{
	const fs::path iconPath = GetExecutableDir() / "Launcher.ico";
	const DecodedIcon icon  = LoadIconFromFile(iconPath);
	if (!icon.IsValid())
	{
		spdlog::warn("Launcher: couldn't load window icon from {}", iconPath.string());
		return;
	}

	SDL_Surface* pSurface = SDL_CreateSurfaceFrom(
		icon.width, icon.height, SDL_PIXELFORMAT_RGBA32,
		const_cast<uint8_t*>(icon.pixelsRgba.data()), icon.width * 4);
	if (pSurface == nullptr)
	{
		spdlog::warn("Launcher: SDL_CreateSurfaceFrom failed: {}", SDL_GetError());
		return;
	}

	SDL_SetWindowIcon(pWindow, pSurface);
	SDL_DestroySurface(pSurface);
}

// Same flat, rounded "modern" dark theme as Option (see OptionMainSDL.cpp's
// identical function for the rationale) so the two tools look like a matched
// pair instead of one modernized and one left on stock ImGui colors.
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

// Headless path for CI/no-display environments: exercises the packet
// layer without touching a socket (LauncherCore_test.cpp covers that
// against the real protocol layout already).
int RunSmokeTest()
{
	spdlog::info("Launcher: --smoke starting");

	const std::vector<uint8_t> req = launcher_core::BuildVersionRequest(CLIENT_VERSION);
	const std::vector<uint8_t> frame = launcher_core::FrameMessage(req);

	std::vector<uint8_t> buffer = frame;
	std::vector<uint8_t> payload;
	if (!launcher_core::TryExtractFrame(buffer, payload) || payload != req)
	{
		spdlog::error("Launcher: smoke test failed - frame round-trip mismatch");
		return 1;
	}

	spdlog::info("Launcher: smoke test finished");
	return 0;
}
} // namespace

int main(int argc, char** argv)
{
	bool bSmoke = false;
	std::string dataDirOverride;
	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--smoke") == 0)
			bSmoke = true;
		else if (std::strcmp(argv[i], "--data") == 0 && i + 1 < argc)
			dataDirOverride = argv[i + 1];
	}

	if (bSmoke)
		return RunSmokeTest();

	fs::path resolvedGameDir = FindGameDataDir(dataDirOverride);
	if (resolvedGameDir.empty())
	{
		std::error_code ec;
		resolvedGameDir = fs::current_path(ec);
		spdlog::warn("Launcher: no game data directory found (pass --data <path> or set "
					 "OPENKO_GAME_DATA) - trying '{}'",
			resolvedGameDir.string());
	}
	else
	{
		spdlog::info("Launcher: game data directory: {}", resolvedGameDir.string());
	}

	SharedState shared;
	std::thread networkThread(NetworkThreadMain, std::ref(shared), resolvedGameDir);

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		spdlog::error("Launcher: SDL_Init failed: {}", SDL_GetError());
		networkThread.join();
		return 1;
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	SDL_Window* pWindow =
		SDL_CreateWindow("AUTO UPGRADE LAUNCHER", 540, 260, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
	if (pWindow == nullptr)
	{
		spdlog::error("Launcher: SDL_CreateWindow failed: {}", SDL_GetError());
		SDL_Quit();
		networkThread.join();
		return 1;
	}

	SDL_GLContext glContext = SDL_GL_CreateContext(pWindow);
	if (glContext == nullptr)
	{
		spdlog::error("Launcher: SDL_GL_CreateContext failed: {}", SDL_GetError());
		SDL_DestroyWindow(pWindow);
		SDL_Quit();
		networkThread.join();
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

	bool bRunning = true;
	bool bLaunchRequested = false;
	bool bLaunchFailed = false;
	float launchDelayRemaining = 0.0f;
	uint64_t lastTicks = SDL_GetTicks();

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

		const uint64_t nowTicks = SDL_GetTicks();
		const float deltaSeconds = static_cast<float>(nowTicks - lastTicks) / 1000.0f;
		lastTicks                = nowTicks;

		LauncherState state;
		std::string statusText;
		int serverVersion;
		size_t patchFileCount;
		{
			std::lock_guard<std::mutex> lock(shared.mutex);
			state          = shared.state;
			statusText     = shared.statusText;
			serverVersion  = shared.serverVersion;
			patchFileCount = shared.downloadInfo.fileNames.size();
		}

		if (state == LauncherState::UpToDate && !bLaunchRequested)
		{
			bLaunchRequested     = true;
			launchDelayRemaining = 1.0f; // brief, visible pause before handoff - not an instant jump-cut
		}
		if (bLaunchRequested && !bLaunchFailed)
		{
			launchDelayRemaining -= deltaSeconds;
			if (launchDelayRemaining <= 0.0f)
			{
				if (LaunchWarFareAndExit(resolvedGameDir))
					bRunning = false;
				else
					bLaunchFailed = true; // keep the window open with an error instead of vanishing
			}
		}

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		int windowW = 0, windowH = 0;
		SDL_GetWindowSize(pWindow, &windowW, &windowH);
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(static_cast<float>(windowW), static_cast<float>(windowH)));
		ImGui::Begin("##LauncherRoot", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
				| ImGuiWindowFlags_NoCollapse);

		ImGui::TextWrapped("%s", statusText.c_str());
		if (serverVersion != 0)
			ImGui::Text("Server version: %d (client: %d)", serverVersion, CLIENT_VERSION);

		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.25f, 1.0f, 0.25f, 1.0f));
		const float fraction = (state == LauncherState::Connecting)
									 ? -1.0f * static_cast<float>(SDL_GetTicks() % 2000) / 2000.0f
									 : 1.0f;
		ImGui::ProgressBar(fraction, ImVec2(-1, 0));
		ImGui::PopStyleColor();

		if (state == LauncherState::NeedsUpdate)
		{
			ImGui::Spacing();
			ImGui::TextWrapped("%zu file(s) pending: %s", patchFileCount,
				shared.downloadInfo.fileNames.empty() ? "" : shared.downloadInfo.fileNames.front().c_str());
			if (ImGui::Button("Play Anyway", ImVec2(120, 0)))
			{
				bLaunchRequested     = true;
				launchDelayRemaining = 0.0f;
			}
			ImGui::SameLine();
			if (ImGui::Button("Quit", ImVec2(100, 0)))
				bRunning = false;
		}
		else if (state == LauncherState::Error)
		{
			ImGui::Spacing();
			if (ImGui::Button("Quit", ImVec2(100, 0)))
				bRunning = false;
		}

		if (bLaunchFailed)
		{
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
			ImGui::TextWrapped(
				"Couldn't find the KnightOnLine binary next to Launcher (looked in %s and %s).",
				GetExecutableDir().string().c_str(), resolvedGameDir.string().c_str());
			ImGui::PopStyleColor();
			if (ImGui::Button("Quit", ImVec2(100, 0)))
				bRunning = false;
		}

		ImGui::End();

		ImGui::Render();
		glViewport(0, 0, windowW, windowH);
		glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
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

	networkThread.join();
	return 0;
}
