#ifndef PLATFORM_PLATFORMPATHS_H
#define PLATFORM_PLATFORMPATHS_H

#pragma once

#include <cstdlib>
#include <filesystem>

// Header-only so the Windows MSBuild projects can consume it without linking
// anything new (same rule as PlatformTime.h).

/// \brief Per-user configuration directory for the game, or empty when the
///        platform convention is "next to the executable" (Windows).
///
/// macOS: ~/Library/Application Support/OpenKO
/// Linux: $XDG_CONFIG_HOME/openko (or ~/.config/openko)
inline std::filesystem::path GetUserConfigDir()
{
#if defined(__APPLE__)
	if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
		return std::filesystem::path(home) / "Library" / "Application Support" / "OpenKO";
	return {};
#elif !defined(_WIN32)
	if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0')
		return std::filesystem::path(xdg) / "openko";
	if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
		return std::filesystem::path(home) / ".config" / "openko";
	return {};
#else
	// Windows keeps the game's historical behavior: config lives in the
	// working/executable directory.
	return {};
#endif
}

#endif // PLATFORM_PLATFORMPATHS_H
