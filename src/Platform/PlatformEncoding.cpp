#include "PlatformEncoding.h"

#ifdef _WIN32

#include <windows.h>

namespace
{
constexpr UINT CODEPAGE_KOREAN = 949;

std::wstring MultiByteToWide(std::string_view text, UINT codePage)
{
	if (text.empty())
		return {};

	const int wideLength = ::MultiByteToWideChar(
		codePage, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	if (wideLength <= 0)
		return {};

	std::wstring wide(static_cast<size_t>(wideLength), L'\0');
	::MultiByteToWideChar(
		codePage, 0, text.data(), static_cast<int>(text.size()), wide.data(), wideLength);
	return wide;
}

std::string WideToMultiByte(std::wstring_view text, UINT codePage)
{
	if (text.empty())
		return {};

	const int narrowLength = ::WideCharToMultiByte(
		codePage, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	if (narrowLength <= 0)
		return {};

	std::string narrow(static_cast<size_t>(narrowLength), '\0');
	::WideCharToMultiByte(codePage, 0, text.data(), static_cast<int>(text.size()), narrow.data(),
		narrowLength, nullptr, nullptr);
	return narrow;
}
} // namespace

std::string Cp949ToUtf8(std::string_view cp949Text)
{
	return WideToMultiByte(MultiByteToWide(cp949Text, CODEPAGE_KOREAN), CP_UTF8);
}

std::string Utf8ToCp949(std::string_view utf8Text)
{
	return WideToMultiByte(MultiByteToWide(utf8Text, CP_UTF8), CODEPAGE_KOREAN);
}

#else // POSIX: iconv

#include <cerrno>
#include <iconv.h>

namespace
{
// Encoding names differ across iconv implementations (glibc, libiconv on
// macOS, musl); try the common aliases for the Korean codepage in order.
constexpr const char* KOREAN_ENCODING_NAMES[] = {"CP949", "UHC", "EUC-KR"};

std::string IconvConvert(std::string_view text, const char* toEncoding, const char* fromEncoding)
{
	if (text.empty())
		return {};

	const iconv_t descriptor = iconv_open(toEncoding, fromEncoding);
	if (descriptor == reinterpret_cast<iconv_t>(-1))
		return {};

	std::string result;
	result.reserve(text.size() * 2);

	// iconv mutates its in/out pointers; work on a mutable copy of the view.
	std::string input(text);
	char* inPtr      = input.data();
	size_t inLeft    = input.size();

	char buffer[1024];

	while (inLeft > 0)
	{
		char* outPtr   = buffer;
		size_t outLeft = sizeof(buffer);

		const size_t rc = iconv(descriptor, &inPtr, &inLeft, &outPtr, &outLeft);
		result.append(buffer, sizeof(buffer) - outLeft);

		if (rc == static_cast<size_t>(-1))
		{
			if (errno == E2BIG)
				continue; // output buffer full; flush and go around again

			// EILSEQ/EINVAL: skip the offending byte instead of failing the
			// whole string (legacy assets occasionally carry stray bytes).
			if (inLeft > 0)
			{
				++inPtr;
				--inLeft;
			}
		}
	}

	// Flush any conversion state.
	char* outPtr   = buffer;
	size_t outLeft = sizeof(buffer);
	iconv(descriptor, nullptr, nullptr, &outPtr, &outLeft);
	result.append(buffer, sizeof(buffer) - outLeft);

	iconv_close(descriptor);
	return result;
}

std::string ConvertWithKoreanCodepage(std::string_view text, bool koreanIsSource)
{
	for (const char* koreanName : KOREAN_ENCODING_NAMES)
	{
		const char* to   = koreanIsSource ? "UTF-8" : koreanName;
		const char* from = koreanIsSource ? koreanName : "UTF-8";

		const iconv_t probe = iconv_open(to, from);
		if (probe == reinterpret_cast<iconv_t>(-1))
			continue;
		iconv_close(probe);

		return IconvConvert(text, to, from);
	}

	// No Korean codec available in this libc: pass the bytes through so ASCII
	// content (the vast majority of config/asset strings) still works.
	return std::string(text);
}
} // namespace

std::string Cp949ToUtf8(std::string_view cp949Text)
{
	return ConvertWithKoreanCodepage(cp949Text, true);
}

std::string Utf8ToCp949(std::string_view utf8Text)
{
	return ConvertWithKoreanCodepage(utf8Text, false);
}

#endif // _WIN32
