#ifndef CLIENT_LAUNCHER_LAUNCHERPATCH_H
#define CLIENT_LAUNCHER_LAUNCHERPATCH_H

#pragma once

// Modern patch-download logic for the POSIX Launcher (docs/PORT_POSIX_PLAN.md,
// F10). The original Windows Launcher pulled each patch .zip over FTP
// (WinInet) and extracted it with the MFC-only ZipArchive. FTP is long
// obsolete for software distribution, so this replaces it with HTTP(S) over
// Asio (+ OpenSSL for TLS) and ZIP extraction with miniz - both portable and
// with no MFC/WinInet dependency.
//
// The URL-building and ZIP-extraction pieces are pure (no sockets), so
// they're unit-tested directly; DownloadFile is exercised against a local
// HTTP server in the tests.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace launcher_patch
{

// --- URL handling (pure) ---------------------------------------------------

struct ParsedUrl
{
	std::string scheme; // "http" or "https"
	std::string host;
	std::string port;   // resolved default when absent ("80"/"443")
	std::string target; // path + query, always starts with '/'
	bool valid = false;
};

// Parses an absolute http(s):// URL. Returns valid=false on anything else.
ParsedUrl ParseUrl(const std::string& url);

// Builds the download URL for one patch file from the server-provided
// base host, directory and filename. The base may already be a full
// http(s):// URL (used verbatim as the host+scheme); otherwise http:// is
// assumed - the version-manager protocol predates HTTPS and still calls the
// field an "FTP url", but an admin can point it (or the Server.Ini override)
// at a real HTTP(S) mirror. Slashes are normalized so no doubled or missing
// separators appear.
std::string BuildPatchUrl(
	const std::string& base, const std::string& path, const std::string& fileName);

// --- ZIP extraction (miniz) ------------------------------------------------

// Extracts every entry of `zipPath` under `destDir` (creating subdirectories
// as needed), refusing any entry whose path escapes destDir (zip-slip
// guard). Returns false and fills `error` on any failure.
bool ExtractZip(const std::string& zipPath, const std::string& destDir, std::string& error);

// --- HTTP(S) download (Asio [+ OpenSSL]) -----------------------------------

// Progress callback: (bytesReceived, totalBytes-or-0-if-unknown). Return
// false to abort the transfer.
using ProgressCallback = std::function<bool(uint64_t received, uint64_t total)>;

// Downloads `url` to `destPath`. https:// requires a build with OpenSSL
// (LAUNCHER_HAVE_TLS); without it an https URL fails with a clear error.
// Follows a single level of redirect. Returns false and fills `error` on
// failure.
bool DownloadFile(const std::string& url, const std::string& destPath,
	const ProgressCallback& progress, std::string& error);

} // namespace launcher_patch

#endif // CLIENT_LAUNCHER_LAUNCHERPATCH_H
