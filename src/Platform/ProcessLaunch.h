#ifndef PLATFORM_PROCESSLAUNCH_H
#define PLATFORM_PROCESSLAUNCH_H

#pragma once

// Shared "launch a sibling POSIX client-tool binary and exit" helper. There
// is no Win32 ShellExecute() on POSIX; std::system("<path> &") is the
// fire-and-forget equivalent the original MFC dialogs used it for (Launcher
// starting the game client, the in-game exit menu starting Option, ...).
//
// Pulled out of Option/Launcher's near-identical copies (docs/PORT_POSIX_PLAN.md,
// F10) so WarFare's own "open Option" hookup doesn't need a third copy.

#include <Platform/GameDataDir.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace platform_launch
{

// Wraps a path in double quotes for a shell command line, escaping any
// embedded backslash or double-quote so paths with spaces (common under
// "~/Library/Application Support/...") survive std::system's `sh -c`.
inline std::string ShellQuote(const std::string& s)
{
	std::string out = "\"";
	for (char c : s)
	{
		if (c == '"' || c == '\\')
			out += '\\';
		out += c;
	}
	out += '"';
	return out;
}

// Looks for `exeName` in each of `searchDirs`, in order. On macOS also tries
// the .app bundle layout (<dir>/<exeName>.app/Contents/MacOS/<exeName>):
// WarFare ships as a bundle there (MACOSX_BUNDLE TRUE) while Option/Launcher
// are plain binaries, so a bare "<dir>/<exeName>" check alone never finds
// it. Returns an empty path if nothing matched.
inline std::filesystem::path FindSiblingExecutable(
	const std::vector<std::filesystem::path>& searchDirs, const std::string& exeName)
{
	for (const std::filesystem::path& dir : searchDirs)
	{
		std::error_code ec;

		std::filesystem::path direct = dir / exeName;
		if (std::filesystem::exists(direct, ec) && std::filesystem::is_regular_file(direct, ec))
			return direct;

#if defined(__APPLE__)
		std::filesystem::path bundled = dir / (exeName + ".app") / "Contents" / "MacOS" / exeName;
		if (std::filesystem::exists(bundled, ec) && std::filesystem::is_regular_file(bundled, ec))
			return bundled;
#endif
	}

	return {};
}

// Fire-and-forget launch of `exePath`, optionally forwarding
// "--data <gameDir>" - only when gameDir looks like a real game-data
// directory (LooksLikeGameDataDir) so a CWD-fallback guess doesn't override
// the launched tool's own auto-discovery with a bad one.
inline void LaunchDetached(const std::filesystem::path& exePath, const std::filesystem::path& gameDir)
{
	std::string cmd = ShellQuote(exePath.string());
	if (LooksLikeGameDataDir(gameDir))
		cmd += " --data " + ShellQuote(gameDir.string());
	cmd += " &";
	std::system(cmd.c_str()); // NOLINT(cert-env33-c) - fire-and-forget launch, same spirit as ShellExecute
}

} // namespace platform_launch

#endif // PLATFORM_PROCESSLAUNCH_H
