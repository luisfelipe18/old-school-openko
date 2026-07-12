#ifndef CLIENT_KSCVIEWER_KSCVIEWERCORE_H
#define CLIENT_KSCVIEWER_KSCVIEWERCORE_H

#pragma once

// Platform-neutral core of the KscViewer tool (docs/PORT_POSIX_PLAN.md, F10):
// .ksc files are the client's splash/loading images - a JPEG encrypted with
// the game's classic stream cipher (the same one CJpegFile::Decrypt uses)
// behind an 8-byte header. This holds the decrypt + decode + export logic so
// it is unit-testable without a GUI (see tests/KscViewer); KscViewerMainSDL
// wraps it in a Dear ImGui window, mirroring the Option/Launcher ports.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ksc_core
{

/// Decrypts a .ksc byte buffer into the raw JPEG bytes it wraps. Returns an
/// empty vector when the buffer is too short or the decrypted header isn't
/// the expected "KSC\x01" magic. Self-contained (its own cipher state) so it
/// is reentrant, unlike CJpegFile's static-member cipher.
std::vector<uint8_t> DecryptKsc(const std::vector<uint8_t>& kscBytes);

/// A decoded RGB image, 3 bytes per pixel, top-down rows.
struct DecodedImage
{
	int width  = 0;
	int height = 0;
	std::vector<uint8_t> rgb; // width * height * 3

	bool IsValid() const
	{
		return width > 0 && height > 0
			   && rgb.size() == static_cast<size_t>(width) * height * 3;
	}
};

/// Loads and decodes a .ksc (decrypt then JPEG-decode) or a plain .jpg/.jpeg
/// from disk into RGB pixels. Returns an invalid image on any failure.
DecodedImage LoadImage(const std::filesystem::path& path);

/// Exports the decoded JPEG of a .ksc (or a copy of a .jpg) to \p dstJpg.
/// Returns false on any failure.
bool ExportJpg(const std::filesystem::path& src, const std::filesystem::path& dstJpg);

/// True when the path has a .ksc extension (case-insensitive).
bool IsKscPath(const std::filesystem::path& path);

} // namespace ksc_core

#endif // CLIENT_KSCVIEWER_KSCVIEWERCORE_H
