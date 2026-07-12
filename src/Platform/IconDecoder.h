#ifndef PLATFORM_ICONDECODER_H
#define PLATFORM_ICONDECODER_H

#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

// Decoder for classic Windows .ico icon files shipped with the client tools
// (Option.ico, Launcher.ico, ...). On Windows they're embedded as resources
// and loaded automatically from the .rc's IDI_ICON1; the POSIX ImGui/SDL
// port has no resource compiler, so this decodes the file directly into RGBA
// pixels for SDL_SetWindowIcon(). Same container family as the client's .cur
// cursors (see Client/WarFare/CursorDecoder.h): an ICONDIR of type 1 instead
// of type 2, and a colour-count/reserved pair in each directory entry instead
// of a hotspot.

struct DecodedIcon
{
	int width  = 0;
	int height = 0;
	std::vector<uint8_t> pixelsRgba; // width * height * 4, top-down rows

	bool IsValid() const
	{
		return width > 0 && height > 0
			   && pixelsRgba.size() == static_cast<size_t>(width) * height * 4;
	}
};

/// \brief Decodes the largest supported image in a .ico file buffer.
/// \returns An icon with IsValid() == false when the data is not a supported
///          icon (PNG-compressed directory entries, present in modern .ico
///          files, are not supported - none of the client's original .ico
///          resources use them).
DecodedIcon DecodeIconFile(const std::vector<uint8_t>& fileData);

/// \brief Convenience wrapper: reads and decodes a .ico file from disk.
DecodedIcon LoadIconFromFile(const std::filesystem::path& path);

#endif // PLATFORM_ICONDECODER_H
