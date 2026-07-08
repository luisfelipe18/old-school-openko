#ifndef CLIENT_WARFARE_CURSORDECODER_H
#define CLIENT_WARFARE_CURSORDECODER_H

#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

// Decoder for the classic Windows .cur cursor files shipped with the client
// (Cursor_Normal.cur, ...). On Windows they're embedded as resources and
// loaded with LoadCursor(); the POSIX port decodes them into RGBA pixels and
// hands them to SDL_CreateColorCursor(). Pure code, unit-tested against the
// real cursor files.

struct DecodedCursor
{
	int width    = 0;
	int height   = 0;
	int hotspotX = 0;
	int hotspotY = 0;
	std::vector<uint8_t> pixelsRgba; // width * height * 4, top-down rows

	bool IsValid() const
	{
		return width > 0 && height > 0
			   && pixelsRgba.size() == static_cast<size_t>(width) * height * 4;
	}
};

/// \brief Decodes the first image of a .cur file buffer.
/// \returns A cursor with IsValid() == false when the data is not a
///          supported cursor (1/4/8-bit paletted, 24-bit or 32-bit BMP data).
DecodedCursor DecodeCursorFile(const std::vector<uint8_t>& fileData);

/// \brief Convenience wrapper: reads and decodes a .cur file from disk.
DecodedCursor LoadCursorFromFile(const std::filesystem::path& path);

#endif // CLIENT_WARFARE_CURSORDECODER_H
