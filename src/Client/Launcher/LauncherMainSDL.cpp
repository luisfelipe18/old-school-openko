// POSIX entry point for the Launcher tool (docs/PORT_POSIX_PLAN.md, F10):
// the original is an MFC dialog (LauncherDlg.cpp). This reimplements the
// full flow on the SDL3 + Dear ImGui + OpenGL stack the other tool ports
// use:
//   - connect to the same server list WarFare's login scene uses and ask
//     VersionManager for its version (LauncherCore.h - byte-identical
//     framing/packets to the original);
//   - if the server is ahead, fetch the patch file list and, on START,
//     download each .zip and extract it into the game data directory
//     (LauncherPatch.h - HTTP[S] over Asio + miniz, replacing the
//     original's obsolete FTP + Windows-only ZipArchive), then launch;
//   - if versions match, launch the game directly.
//
// The UI recreates the classic launcher chrome (see the reference shots in
// the F10 notes): the game's own res/Bkg.bmp artwork as the background, a
// news panel (top-right, admin-configured via Server.Ini [Launcher]), an
// amber download progress bar, and START / HOME PAGE / OPTION buttons.
//
// The login-via-Launcher path (LOGIN_REQ/SERVER_LIST) is deliberately NOT
// reimplemented - see LauncherCore.h: it's dead code against the current
// protocol even in the original Windows binary.

#include "LauncherCore.h"
#include "LauncherPatch.h"

#include <Platform/GameDataDir.h>
#include <Platform/IconDecoder.h>
#include <Platform/PlatformIni.h>
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
#include <fstream>
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
	Downloading,
	Error,
};

struct SharedState
{
	std::mutex mutex;
	LauncherState state = LauncherState::Connecting;
	std::string statusText = "Connecting...";
	int serverVersion      = 0;
	launcher_core::DownloadInfoResponse downloadInfo;

	// Patch download progress (guarded by mutex; read each UI frame).
	int currentFile      = 0;   // 1-based index of the file being fetched
	int totalFiles       = 0;
	float fileProgress   = 0.0f; // 0..1 of the current file
	std::string patchBaseUrl;    // Server.Ini [Patch] BaseUrl override, if any

	// Set once by the UI thread to kick off DownloadThreadMain.
	std::atomic<bool> downloadRequested{ false };
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
	const fs::path serverIni = gameDir / "Server.Ini";
	const std::vector<std::string> ips = launcher_core::ReadServerIpList(serverIni.string());
	if (ips.empty())
	{
		// Spell out the exact path so a wrong game-data directory (the usual
		// cause) is visible in the window itself, not just in the log.
		SetStatus(shared, LauncherState::Error,
			"No servers listed in " + serverIni.string()
				+ " - point --data (or OPENKO_GAME_DATA) at the folder holding Server.Ini");
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

	// Optional Server.Ini override so a server admin can point the launcher at
	// a modern HTTP(S) mirror/CDN without changing the version-manager
	// protocol (which still calls the field an "FTP url"):
	//   [Patch]
	//   BaseUrl=https://cdn.example.com/ko-patches
	std::array<char, 512> baseUrl {};
	GetPrivateProfileString("Patch", "BaseUrl", "", baseUrl.data(),
		static_cast<DWORD>(baseUrl.size()), serverIni.string().c_str());

	{
		std::lock_guard<std::mutex> lock(shared.mutex);
		shared.downloadInfo = downloadInfo;
		shared.patchBaseUrl = baseUrl.data();
		shared.totalFiles   = static_cast<int>(downloadInfo.fileNames.size());
		shared.state        = LauncherState::NeedsUpdate;
		shared.statusText   = "Update available (" + std::to_string(downloadInfo.fileNames.size())
							 + " file(s))";
	}
}

// Worker: downloads each patch .zip over HTTP(S) and extracts it into the
// game data directory, updating progress in `shared`. On success flips the
// state to UpToDate so the main loop's existing auto-launch kicks in.
void DownloadThreadMain(SharedState& shared, fs::path gameDir)
{
	launcher_core::DownloadInfoResponse info;
	std::string baseOverride;
	{
		std::lock_guard<std::mutex> lock(shared.mutex);
		info         = shared.downloadInfo;
		baseOverride = shared.patchBaseUrl;
	}

	std::error_code ec;
	const fs::path tmpDir = fs::temp_directory_path() / "openko-patches";
	fs::create_directories(tmpDir, ec);

	for (size_t i = 0; i < info.fileNames.size(); ++i)
	{
		const std::string& name = info.fileNames[i];
		// The Server.Ini override is the complete base directory (admin owns
		// the layout), so only the filename is appended. Without an override,
		// use the host + directory the version manager sent.
		const std::string url =
			baseOverride.empty()
				? launcher_patch::BuildPatchUrl(info.ftpUrl, info.ftpPath, name)
				: launcher_patch::BuildPatchUrl(baseOverride, "", name);
		const fs::path dst        = tmpDir / name;

		{
			std::lock_guard<std::mutex> lock(shared.mutex);
			shared.state       = LauncherState::Downloading;
			shared.currentFile = static_cast<int>(i + 1);
			shared.fileProgress = 0.0f;
			shared.statusText  = "Downloading " + name + " ...";
		}
		spdlog::info("Launcher: downloading patch {} from {}", name, url);

		std::string error;
		const bool ok = launcher_patch::DownloadFile(url, dst.string(),
			[&](uint64_t received, uint64_t total) {
				std::lock_guard<std::mutex> lock(shared.mutex);
				shared.fileProgress = (total > 0)
										  ? static_cast<float>(static_cast<double>(received) / total)
										  : 0.0f;
				return true;
			},
			error);

		if (!ok)
		{
			SetStatus(shared, LauncherState::Error, "Download failed for " + name + ": " + error);
			return;
		}

		{
			std::lock_guard<std::mutex> lock(shared.mutex);
			shared.statusText = "Installing " + name + " ...";
		}
		if (!launcher_patch::ExtractZip(dst.string(), gameDir.string(), error))
		{
			SetStatus(shared, LauncherState::Error, "Install failed for " + name + ": " + error);
			return;
		}
		fs::remove(dst, ec);
	}

	SetStatus(shared, LauncherState::UpToDate, "Update complete - starting game...");
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

// --- Original-launcher UI assets --------------------------------------------

// Minimal BMP decoder for the launcher's own background artwork (res/Bkg.bmp:
// a bottom-up, uncompressed 8-bit-paletted or 24-bit DIB) into top-down RGBA.
// The original Launcher.rc compiled this in as IDB_BKG; here it becomes the
// window background, matching the classic launcher's look.
struct DecodedBmp
{
	int width  = 0;
	int height = 0;
	std::vector<uint8_t> rgba; // width * height * 4, top-down
	bool IsValid() const
	{
		return width > 0 && height > 0
			   && rgba.size() == static_cast<size_t>(width) * height * 4;
	}
};

uint32_t ReadU32LE(const uint8_t* p)
{
	return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24));
}

DecodedBmp LoadBmp(const fs::path& path)
{
	DecodedBmp out;
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return out;
	std::vector<uint8_t> d((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	if (d.size() < 54 || d[0] != 'B' || d[1] != 'M')
		return out;

	const uint32_t dataOffset = ReadU32LE(&d[10]);
	const uint32_t headerSize = ReadU32LE(&d[14]);
	const int width           = static_cast<int32_t>(ReadU32LE(&d[18]));
	const int rawHeight       = static_cast<int32_t>(ReadU32LE(&d[22]));
	const uint16_t bpp        = static_cast<uint16_t>(d[28] | (d[29] << 8));
	const uint32_t compression = ReadU32LE(&d[30]);
	if (width <= 0 || rawHeight == 0 || compression != 0)
		return out;
	if (bpp != 8 && bpp != 24 && bpp != 32)
		return out;

	const bool topDown = rawHeight < 0;
	const int height   = topDown ? -rawHeight : rawHeight;

	// 8-bit paletted: the palette (BGRA quads) sits right after the header.
	const uint8_t* palette = nullptr;
	if (bpp == 8)
		palette = &d[14 + headerSize];

	const int bytesPerPixel = bpp / 8;
	const size_t rowStride  = ((static_cast<size_t>(width) * bytesPerPixel + 3) / 4) * 4;
	if (dataOffset + rowStride * height > d.size())
		return out;

	out.width  = width;
	out.height = height;
	out.rgba.assign(static_cast<size_t>(width) * height * 4, 0);

	for (int y = 0; y < height; ++y)
	{
		const int srcY      = topDown ? y : (height - 1 - y); // BMP rows are bottom-up
		const uint8_t* row  = &d[dataOffset + rowStride * srcY];
		uint8_t* dst        = &out.rgba[static_cast<size_t>(y) * width * 4];
		for (int x = 0; x < width; ++x, dst += 4)
		{
			uint8_t r = 0, g = 0, b = 0, a = 255;
			if (bpp == 8)
			{
				const uint8_t* c = palette + row[x] * 4; // BGRA
				b = c[0];
				g = c[1];
				r = c[2];
			}
			else
			{
				b = row[x * bytesPerPixel + 0];
				g = row[x * bytesPerPixel + 1];
				r = row[x * bytesPerPixel + 2];
				if (bpp == 32)
					a = row[x * bytesPerPixel + 3];
			}
			dst[0] = r;
			dst[1] = g;
			dst[2] = b;
			dst[3] = a;
		}
	}
	return out;
}

GLuint UploadRgbaTexture(const DecodedBmp& bmp)
{
	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bmp.width, bmp.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
		bmp.rgba.data());
	glBindTexture(GL_TEXTURE_2D, 0);
	return tex;
}

// Opens a URL in the user's default browser (HOME PAGE / social links).
void OpenUrl(const std::string& url)
{
	if (url.empty())
		return;
	std::string quoted = "\"";
	for (char c : url)
	{
		if (c == '"' || c == '\\')
			quoted += '\\';
		quoted += c;
	}
	quoted += '"';
#if defined(__APPLE__)
	std::system(("open " + quoted + " >/dev/null 2>&1 &").c_str()); // NOLINT(cert-env33-c)
#else
	std::system(("xdg-open " + quoted + " >/dev/null 2>&1 &").c_str()); // NOLINT(cert-env33-c)
#endif
}

// Finds and launches the ported Option tool (next to this binary, or the
// sibling .app on macOS), same search the WarFare hookup uses.
void LaunchOptionTool(const fs::path& gameDir)
{
	std::vector<fs::path> dirs = { GetExecutableDir(), gameDir };
#if defined(__APPLE__)
	dirs.push_back(GetExecutableDir() / ".." / ".." / "..");
#endif
	const fs::path option = platform_launch::FindSiblingExecutable(dirs, "Option");
	if (option.empty())
	{
		spdlog::warn("Launcher: Option tool not found next to this binary");
		return;
	}
	platform_launch::LaunchDetached(option, gameDir);
}

// One news line shown in the launcher's news panel: a label plus an optional
// URL that opens in the browser when clicked.
struct NewsItem
{
	std::string label;
	std::string url;
};

// One social/community link button.
struct SocialLink
{
	std::string label;
	std::string url;
};

// Launcher chrome configured from Server.Ini's [Launcher] section so a server
// admin can set the homepage, news items and social links without a rebuild:
//   [Launcher]
//   HomePage=https://example.com
//   News0=Server merge on 03/01|https://example.com/news/1
//   Discord=https://discord.gg/xxxx
struct LauncherChrome
{
	std::string homePage;
	std::vector<NewsItem> news;
	std::vector<SocialLink> socials;
};

LauncherChrome LoadChrome(const fs::path& serverIni)
{
	LauncherChrome chrome;
	std::array<char, 512> buf {};

	GetPrivateProfileString("Launcher", "HomePage", "", buf.data(),
		static_cast<DWORD>(buf.size()), serverIni.string().c_str());
	chrome.homePage = buf.data();

	// News0..News9 = "label|url" (url optional).
	for (int i = 0; i < 10; ++i)
	{
		const std::string key = "News" + std::to_string(i);
		buf.fill('\0');
		GetPrivateProfileString("Launcher", key.c_str(), "", buf.data(),
			static_cast<DWORD>(buf.size()), serverIni.string().c_str());
		std::string line = buf.data();
		if (line.empty())
			continue;
		NewsItem item;
		const size_t bar = line.find('|');
		item.label = (bar == std::string::npos) ? line : line.substr(0, bar);
		if (bar != std::string::npos)
			item.url = line.substr(bar + 1);
		chrome.news.push_back(std::move(item));
	}

	// Well-known social links, each an optional URL.
	for (const char* name : { "Discord", "Facebook", "Twitter", "Youtube", "Twitch" })
	{
		buf.fill('\0');
		GetPrivateProfileString("Launcher", name, "", buf.data(),
			static_cast<DWORD>(buf.size()), serverIni.string().c_str());
		if (buf[0] != '\0')
			chrome.socials.push_back({ name, buf.data() });
	}

	return chrome;
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

	SDL_Window* pWindow = SDL_CreateWindow(
		"Knight OnLine - Launcher", 640, 560, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
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

	// Background artwork (the classic launcher's own Bkg.bmp) + the
	// admin-configurable news/homepage/social chrome.
	GLuint backgroundTex = 0;
	{
		const DecodedBmp bkg = LoadBmp(GetExecutableDir() / "Bkg.bmp");
		if (bkg.IsValid())
			backgroundTex = UploadRgbaTexture(bkg);
	}
	const LauncherChrome chrome = LoadChrome(resolvedGameDir / "Server.Ini");

	bool bRunning = true;
	bool bLaunchRequested = false;
	bool bLaunchFailed = false;
	bool bDownloadThreadStarted = false;
	std::thread downloadThread;
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
		size_t patchFileCount;
		int currentFile;
		int totalFiles;
		float fileProgress;
		{
			std::lock_guard<std::mutex> lock(shared.mutex);
			state          = shared.state;
			statusText     = shared.statusText;
			patchFileCount = shared.downloadInfo.fileNames.size();
			currentFile    = shared.currentFile;
			totalFiles     = shared.totalFiles;
			fileProgress   = shared.fileProgress;
		}

		// Kick the download worker once the UI (START) requests it.
		if (shared.downloadRequested.load() && !bDownloadThreadStarted)
		{
			bDownloadThreadStarted = true;
			downloadThread = std::thread(DownloadThreadMain, std::ref(shared), resolvedGameDir);
		}

		// A finished update (or an already-current client) auto-launches.
		if (state == LauncherState::UpToDate && !bLaunchRequested)
		{
			bLaunchRequested     = true;
			launchDelayRemaining = 1.0f; // brief, visible pause before handoff
		}
		if (bLaunchRequested && !bLaunchFailed)
		{
			launchDelayRemaining -= deltaSeconds;
			if (launchDelayRemaining <= 0.0f)
			{
				if (LaunchWarFareAndExit(resolvedGameDir))
					bRunning = false;
				else
					bLaunchFailed = true;
			}
		}

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		int windowW = 0, windowH = 0;
		SDL_GetWindowSize(pWindow, &windowW, &windowH);
		const float fw = static_cast<float>(windowW);
		const float fh = static_cast<float>(windowH);

		// --- Background artwork, stretched to fill the window ---
		if (backgroundTex != 0)
		{
			ImGui::GetBackgroundDrawList()->AddImage(static_cast<ImTextureID>(backgroundTex),
				ImVec2(0, 0), ImVec2(fw, fh));
		}
		else
		{
			ImGui::GetBackgroundDrawList()->AddRectFilled(
				ImVec2(0, 0), ImVec2(fw, fh), IM_COL32(20, 18, 16, 255));
		}

		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(fw, fh));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0)); // transparent over the art
		ImGui::Begin("##LauncherRoot", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
				| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
				| ImGuiWindowFlags_NoBringToFrontOnFocus);

		// --- News panel (top-right), semi-transparent over the artwork ---
		const float panelW = fw * 0.52f;
		const float panelH = fh * 0.42f;
		ImGui::SetCursorPos(ImVec2(fw - panelW - 18.0f, 18.0f));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.06f, 0.82f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.42f, 0.18f, 0.9f));
		if (ImGui::BeginChild("##news", ImVec2(panelW, panelH), ImGuiChildFlags_Borders))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.80f, 0.35f, 1.0f));
			ImGui::TextUnformatted("News");
			ImGui::PopStyleColor();
			ImGui::Separator();

			if (chrome.news.empty())
			{
				ImGui::TextDisabled("No news configured.");
				ImGui::TextDisabled("(Set [Launcher] News0=..|url in Server.Ini)");
			}
			for (const NewsItem& item : chrome.news)
			{
				if (!item.url.empty())
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.78f, 1.0f, 1.0f));
					ImGui::TextUnformatted(("- " + item.label).c_str());
					ImGui::PopStyleColor();
					if (ImGui::IsItemHovered())
						ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
					if (ImGui::IsItemClicked())
						OpenUrl(item.url);
				}
				else
				{
					ImGui::BulletText("%s", item.label.c_str());
				}
			}

			if (!chrome.socials.empty())
			{
				ImGui::Separator();
				for (size_t i = 0; i < chrome.socials.size(); ++i)
				{
					if (i > 0)
						ImGui::SameLine();
					if (ImGui::SmallButton(chrome.socials[i].label.c_str()))
						OpenUrl(chrome.socials[i].url);
				}
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleColor(2);

		// --- Bottom strip: status + progress bar + action buttons ---
		const float stripH = 96.0f;
		ImGui::SetCursorPos(ImVec2(18.0f, fh - stripH));

		// Status line.
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
		if (state == LauncherState::Downloading && totalFiles > 0)
			ImGui::Text("[%d/%d] %s", currentFile, totalFiles, statusText.c_str());
		else
			ImGui::TextWrapped("%s", statusText.c_str());
		ImGui::PopStyleColor();

		// Progress bar - amber, fills with the download (indeterminate while
		// connecting, per-file fraction while downloading).
		float fraction = 1.0f;
		if (state == LauncherState::Connecting)
			fraction = -1.0f * static_cast<float>(SDL_GetTicks() % 2000) / 2000.0f;
		else if (state == LauncherState::Downloading)
			fraction = fileProgress;
		else if (state == LauncherState::NeedsUpdate)
			fraction = 0.0f;
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.85f, 0.42f, 0.20f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.08f, 0.06f, 0.9f));
		ImGui::ProgressBar(fraction, ImVec2(fw - 36.0f, 16.0f));
		ImGui::PopStyleColor(2);

		ImGui::Spacing();

		// START / HOME PAGE / OPTION.
		const bool busy = (state == LauncherState::Connecting
			|| state == LauncherState::Downloading || bLaunchRequested);

		ImGui::BeginDisabled(busy);
		if (ImGui::Button("START", ImVec2(150, 34)))
		{
			if (state == LauncherState::NeedsUpdate && patchFileCount > 0)
				shared.downloadRequested.store(true); // download, then auto-launch
			else
			{
				bLaunchRequested     = true;
				launchDelayRemaining = 0.0f;
			}
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		ImGui::BeginDisabled(chrome.homePage.empty());
		if (ImGui::Button("HOME PAGE", ImVec2(150, 34)))
			OpenUrl(chrome.homePage);
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("OPTION", ImVec2(150, 34)))
			LaunchOptionTool(resolvedGameDir);

		if (bLaunchFailed)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
			ImGui::TextWrapped("Couldn't find the KnightOnLine binary next to Launcher.");
			ImGui::PopStyleColor();
		}

		ImGui::End();
		ImGui::PopStyleColor(); // WindowBg

		ImGui::Render();
		glViewport(0, 0, windowW, windowH);
		glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(pWindow);
	}

	if (backgroundTex != 0)
		glDeleteTextures(1, &backgroundTex);

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	SDL_GL_DestroyContext(glContext);
	SDL_DestroyWindow(pWindow);
	SDL_Quit();

	networkThread.join();
	if (downloadThread.joinable())
		downloadThread.join();
	return 0;
}
