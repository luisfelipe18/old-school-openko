#ifndef CLIENT_WARFARE_LAUNCHOPTIONTOOL_H
#define CLIENT_WARFARE_LAUNCHOPTIONTOOL_H

#pragma once

// POSIX counterpart of the in-game exit menu's
// ShellExecute(..., "Option.exe", ...) call (UIExitMenu.cpp,
// UIMessageBox.cpp): there is no ShellExecute on POSIX, and Option ships as
// a separate binary next to WarFare rather than a Windows resource, so this
// finds and launches it the same way Option/Launcher find and launch
// WarFare (Platform/ProcessLaunch.h).

#ifndef _WIN32

#include <N3Base/N3Base.h>

#include <Platform/GameDataDir.h>
#include <Platform/PlatformPaths.h>
#include <Platform/ProcessLaunch.h>

#include <spdlog/spdlog.h>

inline void LaunchOptionTool()
{
	std::vector<std::filesystem::path> searchDirs = { GetExecutableDir() };
#if defined(__APPLE__)
	// WarFare's executable lives inside the bundle (Contents/MacOS/) while
	// the plain-binary tools sit NEXT TO the .app - search there too.
	searchDirs.push_back(GetExecutableDir() / ".." / ".." / "..");
#endif
	const std::filesystem::path candidate =
		platform_launch::FindSiblingExecutable(searchDirs, "Option");

	if (candidate.empty())
	{
		spdlog::warn("WarFare: couldn't find the Option binary next to this executable.");
		return;
	}

	// Point Option at the same game data directory WarFare is actually
	// running against, so its Option.ini/Server.Ini match (same spirit as
	// Option/Launcher forwarding --data back to WarFare on the way in).
	spdlog::info("WarFare: launching {}", candidate.string());
	platform_launch::LaunchDetached(candidate, std::filesystem::path(CN3Base::PathGet()));
}

#endif // !_WIN32

#endif // CLIENT_WARFARE_LAUNCHOPTIONTOOL_H
