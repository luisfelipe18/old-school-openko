#ifndef PLATFORM_GAMEDATADIR_H
#define PLATFORM_GAMEDATADIR_H

#pragma once

// Resolves the OpenKO "game data directory" - the folder holding
// Server.Ini/Option.ini/Data/ - shared by every POSIX client tool (WarFare,
// Option, ...) so they all find the same install regardless of where their
// own binary happens to live (build tree, an installed bin/, or a macOS
// .app bundle). Extracted from WarFareMainSDL.cpp (docs/PORT_POSIX_PLAN.md,
// F8) so the client tool ports (F10) share the exact same discovery rules
// instead of drifting.

#ifndef _WIN32

#include "PlatformPaths.h" // GetExecutableDir

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

inline bool LooksLikeGameDataDir(const std::filesystem::path& p)
{
	std::error_code ec;
	return std::filesystem::exists(p / "Data", ec) || std::filesystem::exists(p / "Server.Ini", ec)
		   || std::filesystem::exists(p / "Server.ini", ec);
}

/// Precedence: \p explicitOverride (e.g. a --data flag) > OPENKO_GAME_DATA
/// env var > CWD if it looks like a data dir > the executable's own
/// directory (and, on macOS, the surrounding .app bundle's usual GameData/
/// Resources slots) > well-known user directories (~/GameData, ...).
/// Returns an empty path when nothing matches.
inline std::filesystem::path FindGameDataDir(const std::string& explicitOverride = {})
{
	if (!explicitOverride.empty())
		return explicitOverride;

	if (const char* envDir = std::getenv("OPENKO_GAME_DATA"); envDir != nullptr && envDir[0] != '\0')
		return envDir;

	std::vector<std::filesystem::path> candidates;

	std::error_code cwdEc;
	candidates.push_back(std::filesystem::current_path(cwdEc));

	if (std::filesystem::path exeDir = GetExecutableDir(); !exeDir.empty())
	{
		// The build system stages assets/Client/ under GameData/ next to the
		// binary (Linux) or inside Contents/Resources/ (macOS bundle), so
		// those slots take priority over the raw exe dir.
		candidates.push_back(exeDir / "GameData");
		candidates.push_back(exeDir);
#if defined(__APPLE__)
		// macOS bundle-relative locations. GameData/ inside Resources/ is
		// the self-contained default the CMake POST_BUILD produces.
		candidates.push_back(exeDir / ".." / "Resources" / "GameData");
		candidates.push_back(exeDir / ".." / "Resources");
		// A data dir next to the .app is another common install layout.
		candidates.push_back(exeDir / ".." / ".." / ".." / "GameData");
		candidates.push_back(exeDir / ".." / ".." / "..");
#endif
	}
	if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
	{
		candidates.push_back(std::filesystem::path(home) / "GameData");
#if defined(__APPLE__)
		candidates.push_back(
			std::filesystem::path(home) / "Library" / "Application Support" / "OpenKO" / "GameData");
#else
		candidates.push_back(std::filesystem::path(home) / ".local" / "share" / "openko" / "GameData");
#endif
	}

	for (const std::filesystem::path& c : candidates)
	{
		std::error_code ec;
		std::filesystem::path canonical = std::filesystem::weakly_canonical(c, ec);
		if (!ec && LooksLikeGameDataDir(canonical))
			return canonical;
	}

	return {};
}

#endif // !_WIN32

#endif // PLATFORM_GAMEDATADIR_H
