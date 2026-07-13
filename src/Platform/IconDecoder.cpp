#include "IconDecoder.h"

#include <fstream>

namespace
{
uint16_t ReadU16(const uint8_t* p)
{
	return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t ReadU32(const uint8_t* p)
{
	return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24));
}

// Rows in BMP data are bottom-up and padded to 4-byte boundaries.
size_t RowStride(size_t bitsPerRow)
{
	return ((bitsPerRow + 31) / 32) * 4;
}

// Decodes the DIB (BITMAPINFOHEADER + optional palette + XOR/AND data) at
// `dataOffset`, the same layout .cur cursors use (CursorDecoder.cpp) - .ico
// just wraps it in a different directory entry shape.
DecodedIcon DecodeDib(const std::vector<uint8_t>& fileData, uint32_t dataOffset)
{
	DecodedIcon icon;

	if (fileData.size() < dataOffset + 40)
		return icon;
	const uint8_t* bih = &fileData[dataOffset];
	if (ReadU32(bih) != 40)
		return icon; // PNG-compressed entries (0x89504E47) or unknown header: unsupported.

	const int width         = static_cast<int32_t>(ReadU32(bih + 4));
	const int xorAndHeight   = static_cast<int32_t>(ReadU32(bih + 8));
	const int height         = xorAndHeight / 2; // stacked XOR + AND masks
	const uint16_t bitCount  = ReadU16(bih + 14);

	if (width <= 0 || width > 512 || height <= 0 || height > 512)
		return icon;
	if (bitCount != 1 && bitCount != 4 && bitCount != 8 && bitCount != 24 && bitCount != 32)
		return icon;

	const size_t paletteCount = (bitCount <= 8) ? (size_t(1) << bitCount) : 0;
	const uint8_t* palette    = bih + 40;

	const uint8_t* xorData = palette + paletteCount * 4;
	const size_t xorStride = RowStride(static_cast<size_t>(width) * bitCount);
	const uint8_t* andData = xorData + xorStride * height;
	const size_t andStride = RowStride(static_cast<size_t>(width));

	const uint8_t* fileEnd = fileData.data() + fileData.size();
	if (andData + andStride * height > fileEnd)
		return icon;

	icon.width  = width;
	icon.height = height;
	icon.pixelsRgba.assign(static_cast<size_t>(width) * height * 4, 0);

	for (int y = 0; y < height; ++y)
	{
		const int srcY        = height - 1 - y; // bottom-up
		const uint8_t* xorRow = xorData + xorStride * srcY;
		const uint8_t* andRow = andData + andStride * srcY;
		uint8_t* dst           = &icon.pixelsRgba[static_cast<size_t>(y) * width * 4];

		for (int x = 0; x < width; ++x, dst += 4)
		{
			uint8_t r = 0, g = 0, b = 0, a = 255;

			switch (bitCount)
			{
				case 1:
				case 4:
				case 8:
				{
					uint32_t index = 0;
					if (bitCount == 1)
						index = (xorRow[x / 8] >> (7 - (x % 8))) & 0x1;
					else if (bitCount == 4)
						index = (xorRow[x / 2] >> ((x % 2) ? 0 : 4)) & 0xF;
					else
						index = xorRow[x];

					const uint8_t* color = palette + index * 4; // BGRA order (BGR0)
					b = color[0];
					g = color[1];
					r = color[2];
					break;
				}

				case 24:
					b = xorRow[x * 3 + 0];
					g = xorRow[x * 3 + 1];
					r = xorRow[x * 3 + 2];
					break;

				case 32:
					b = xorRow[x * 4 + 0];
					g = xorRow[x * 4 + 1];
					r = xorRow[x * 4 + 2];
					a = xorRow[x * 4 + 3];
					break;

				default:
					break;
			}

			if (bitCount != 32 || a == 0)
			{
				const bool transparent = ((andRow[x / 8] >> (7 - (x % 8))) & 0x1) != 0;
				if (bitCount == 32 && !transparent)
					a = 255; // legacy 32-bit icons with an all-zero alpha channel
				else if (transparent)
					a = 0;
			}

			dst[0] = r;
			dst[1] = g;
			dst[2] = b;
			dst[3] = a;
		}
	}

	return icon;
}
} // namespace

DecodedIcon DecodeIconFile(const std::vector<uint8_t>& fileData)
{
	// ICONDIR: reserved(2) type(2, 1=icon) count(2)
	if (fileData.size() < 6 + 16)
		return {};
	if (ReadU16(&fileData[0]) != 0 || ReadU16(&fileData[2]) != 1)
		return {};

	const uint16_t count = ReadU16(&fileData[4]);
	if (count < 1)
		return {};

	// Pick the largest entry (0 in the width/height byte means 256, per the
	// ICONDIRENTRY spec) rather than always the first, since SDL_SetWindowIcon
	// looks best given the biggest image the file offers.
	uint32_t bestArea    = 0;
	uint32_t bestOffset  = 0;
	bool found           = false;

	for (uint16_t i = 0; i < count; ++i)
	{
		const size_t entryOffset = 6 + static_cast<size_t>(i) * 16;
		if (fileData.size() < entryOffset + 16)
			break;
		const uint8_t* entry = &fileData[entryOffset];

		const uint32_t w = entry[0] == 0 ? 256 : entry[0];
		const uint32_t h = entry[1] == 0 ? 256 : entry[1];
		const uint32_t dataOffset = ReadU32(entry + 12);
		const uint32_t area       = w * h;

		if (area > bestArea)
		{
			bestArea   = area;
			bestOffset = dataOffset;
			found      = true;
		}
	}

	if (!found)
		return {};

	return DecodeDib(fileData, bestOffset);
}

DecodedIcon LoadIconFromFile(const std::filesystem::path& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return {};

	std::vector<uint8_t> data(
		(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	return DecodeIconFile(data);
}
