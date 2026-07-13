#include "KscViewerCore.h"

#include <JpegFile/JpegFile.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <system_error>

namespace ksc_core
{
namespace
{
// The classic Borland/Dr.Dobbs stream cipher the game uses (identical to
// CJpegFile::Decrypt, but as local state so it's reentrant). The .ksc layout:
// 8 header bytes (4 discarded + the decrypted magic "KSC\x01"), then the JPEG
// payload, all run through the same keystream continuously.
struct Cipher
{
	uint16_t r = 1124;
	static constexpr uint16_t c1 = 52845;
	static constexpr uint16_t c2 = 22719;

	uint8_t Decrypt(uint8_t cipher)
	{
		const uint8_t plain = static_cast<uint8_t>(cipher ^ (r >> 8));
		r = static_cast<uint16_t>((cipher + r) * c1 + c2);
		return plain;
	}
};

std::vector<uint8_t> ReadFile(const std::filesystem::path& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return {};
	return std::vector<uint8_t>(
		(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

bool WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file)
		return false;
	if (!bytes.empty())
		file.write(reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
	return static_cast<bool>(file);
}

std::string LowerExt(const std::filesystem::path& path)
{
	std::string ext = path.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return ext;
}
} // namespace

std::vector<uint8_t> DecryptKsc(const std::vector<uint8_t>& kscBytes)
{
	if (kscBytes.size() < 8)
		return {};

	Cipher cipher;

	// Bytes 0-3 are decrypted only to advance the keystream (discarded).
	for (size_t i = 0; i < 4; ++i)
		cipher.Decrypt(kscBytes[i]);

	// Bytes 4-7 decrypt to the "KSC\x01" magic.
	uint8_t magic[4];
	for (size_t i = 4; i < 8; ++i)
		magic[i - 4] = cipher.Decrypt(kscBytes[i]);

	if (magic[0] != 'K' || magic[1] != 'S' || magic[2] != 'C' || magic[3] != 1)
		return {};

	std::vector<uint8_t> jpeg;
	jpeg.reserve(kscBytes.size() - 8);
	for (size_t i = 8; i < kscBytes.size(); ++i)
		jpeg.push_back(cipher.Decrypt(kscBytes[i]));

	return jpeg;
}

bool IsKscPath(const std::filesystem::path& path)
{
	return LowerExt(path) == ".ksc";
}

DecodedImage LoadImage(const std::filesystem::path& path)
{
	DecodedImage image;

	// The JPEG decoder (CJpegFile::JpegFileToRGB, libjpeg) reads from a file
	// path, so a .ksc is decrypted into a scratch .jpg first. A .jpg/.jpeg is
	// decoded straight from its own path.
	std::filesystem::path jpegPath = path;
	std::filesystem::path scratch;
	std::error_code ec;

	if (IsKscPath(path))
	{
		const std::vector<uint8_t> jpeg = DecryptKsc(ReadFile(path));
		if (jpeg.empty())
			return image;

		scratch = std::filesystem::temp_directory_path(ec)
				  / ("openko-kscviewer-" + std::to_string(std::hash<std::string>{}(path.string()))
					  + ".jpg");
		if (!WriteFile(scratch, jpeg))
			return image;
		jpegPath = scratch;
	}

	CJpegFile jpegFile;
	UINT width = 0, height = 0;
	BYTE* pRgb = jpegFile.JpegFileToRGB(jpegPath.string(), &width, &height);

	if (!scratch.empty())
		std::filesystem::remove(scratch, ec);

	if (pRgb == nullptr || width == 0 || height == 0)
	{
		delete[] pRgb;
		return image;
	}

	image.width  = static_cast<int>(width);
	image.height = static_cast<int>(height);
	image.rgb.assign(pRgb, pRgb + static_cast<size_t>(width) * height * 3);
	delete[] pRgb;
	return image;
}

bool ExportJpg(const std::filesystem::path& src, const std::filesystem::path& dstJpg)
{
	if (IsKscPath(src))
	{
		const std::vector<uint8_t> jpeg = DecryptKsc(ReadFile(src));
		if (jpeg.empty())
			return false;
		return WriteFile(dstJpg, jpeg);
	}

	// A plain .jpg export is just a copy.
	std::error_code ec;
	std::filesystem::copy_file(
		src, dstJpg, std::filesystem::copy_options::overwrite_existing, ec);
	return !ec;
}

} // namespace ksc_core
