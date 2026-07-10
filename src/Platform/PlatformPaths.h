#ifndef PLATFORM_PLATFORMPATHS_H
#define PLATFORM_PLATFORMPATHS_H

#pragma once

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

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

/// Win32 _makepath shim: assembles a path from its components. Drive is ignored
/// on POSIX; the engine's paths use backslashes as on Windows, so a backslash
/// separator is inserted between dir and fname when the dir lacks a trailing one
/// (matching the CRT's behavior closely enough for the asset-path call sites).
inline void _makepath(char* path, const char* /*drive*/, const char* dir, const char* fname, const char* ext)
{
	std::string out;
	if (dir != nullptr && dir[0] != '\0')
	{
		out = dir;
		char last = out.back();
		if (last != '/' && last != '\\')
			out += '\\';
	}
	if (fname != nullptr)
		out += fname;
	if (ext != nullptr && ext[0] != '\0')
	{
		if (ext[0] != '.')
			out += '.';
		out += ext;
	}
	std::strcpy(path, out.c_str());
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

/// Win32 GetCurrentDirectory shim: writes the working directory into szBuffer.
/// Returns the length written (excluding the null), or 0 on failure.
inline unsigned long GetCurrentDirectory(unsigned long nBufferLength, char* szBuffer)
{
	if (szBuffer == nullptr || nBufferLength == 0)
		return 0;
	std::error_code ec;
	const std::string cwd = std::filesystem::current_path(ec).string();
	if (ec || cwd.size() >= nBufferLength)
		return 0;
	std::strcpy(szBuffer, cwd.c_str());
	return static_cast<unsigned long>(cwd.size());
}
#endif

#endif // PLATFORM_PLATFORMPATHS_H
