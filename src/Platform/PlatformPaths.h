#ifndef PLATFORM_PLATFORMPATHS_H
#define PLATFORM_PLATFORMPATHS_H

#pragma once

#include <cstdlib>
#include <cstring>
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

#ifndef _WIN32
#ifndef _MAX_DRIVE
#define _MAX_DRIVE 3
#define _MAX_DIR   256
#define _MAX_FNAME 256
#define _MAX_EXT   256
#endif

/// Win32 _splitpath shim. Decomposes a path into components; any output pointer
/// may be null. Drive letters do not exist on POSIX (drive is always empty),
/// and both '/' and '\\' are treated as separators so the engine's backslash
/// asset paths decompose the same way they do on Windows.
inline void _splitpath(const char* path, char* drive, char* dir, char* fname, char* ext)
{
	if (drive != nullptr)
		drive[0] = '\0';
	if (dir != nullptr)
		dir[0] = '\0';
	if (fname != nullptr)
		fname[0] = '\0';
	if (ext != nullptr)
		ext[0] = '\0';
	if (path == nullptr)
		return;

	const char* pLastSep = nullptr;
	for (const char* p = path; *p != '\0'; ++p)
	{
		if (*p == '/' || *p == '\\')
			pLastSep = p;
	}

	const char* pName = (pLastSep != nullptr) ? pLastSep + 1 : path;
	if (dir != nullptr && pLastSep != nullptr)
	{
		size_t n = static_cast<size_t>(pName - path);
		std::memcpy(dir, path, n);
		dir[n] = '\0';
	}

	// Extension = the last '.' in the name part, but never a leading dot.
	const char* pDot = nullptr;
	for (const char* p = pName; *p != '\0'; ++p)
	{
		if (*p == '.' && p != pName)
			pDot = p;
	}

	const char* pNameEnd = (pDot != nullptr) ? pDot : (pName + std::strlen(pName));
	if (fname != nullptr)
	{
		size_t n = static_cast<size_t>(pNameEnd - pName);
		std::memcpy(fname, pName, n);
		fname[n] = '\0';
	}
	if (ext != nullptr && pDot != nullptr)
		std::strcpy(ext, pDot);
}

/// Win32 SetCurrentDirectory shim: changes the process working directory.
/// Returns nonzero on success like the Win32 API.
inline int SetCurrentDirectory(const char* szPath)
{
	if (szPath == nullptr)
		return 0;
	std::error_code ec;
	std::filesystem::current_path(szPath, ec);
	return ec ? 0 : 1;
}
#endif

#endif // PLATFORM_PLATFORMPATHS_H
