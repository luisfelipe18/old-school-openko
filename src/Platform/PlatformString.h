#ifndef PLATFORM_PLATFORMSTRING_H
#define PLATFORM_PLATFORMSTRING_H

#pragma once

#include <string>

#ifndef _WIN32
#include <strings.h>

// Win32 case-insensitive compare used by legacy call sites.
inline int lstrcmpi(const char* lhs, const char* rhs)
{
	return strcasecmp(lhs, rhs);
}

// Win32 CRT case-insensitive bounded compare.
inline int _strnicmp(const char* lhs, const char* rhs, size_t count)
{
	return strncasecmp(lhs, rhs, count);
}

// Win32 string helpers used by legacy call sites.
inline int lstrlen(const char* str)
{
	return str ? static_cast<int>(strlen(str)) : 0;
}

inline int lstrcmpA(const char* lhs, const char* rhs)
{
	return strcmp(lhs, rhs);
}

// Win32 lstrcmp (ANSI build): case-sensitive string compare.
inline int lstrcmp(const char* lhs, const char* rhs)
{
	return strcmp(lhs, rhs);
}

// Win32 lstrcmpiA: case-insensitive string compare.
inline int lstrcmpiA(const char* lhs, const char* rhs)
{
	return strcasecmp(lhs, rhs);
}

// Win32 _strlwr: in-place ASCII lowercasing of a C string, returning it. Only
// 'A'..'Z' are touched, matching StrLowerAscii's UTF-8-safe behavior.
inline char* _strlwr(char* str)
{
	if (str != nullptr)
	{
		for (char* p = str; *p != '\0'; ++p)
		{
			if (*p >= 'A' && *p <= 'Z')
				*p += 'a' - 'A';
		}
	}
	return str;
}

// Win32 lstrcpy / lstrcat: thin wrappers over the CRT, returning the
// destination like the Win32 API.
inline char* lstrcpy(char* dest, const char* src)
{
	return strcpy(dest, src);
}

inline char* lstrcat(char* dest, const char* src)
{
	return strcat(dest, src);
}

// Win32 lstrcpyn: copy at most (count-1) chars and always null-terminate within
// count. Returns the destination like the Win32 API.
inline char* lstrcpyn(char* dest, const char* src, int count)
{
	if (dest == nullptr || count <= 0)
		return dest;
	int i = 0;
	if (src != nullptr)
	{
		for (; i < count - 1 && src[i] != '\0'; ++i)
			dest[i] = src[i];
	}
	dest[i] = '\0';
	return dest;
}
#endif

/// \brief In-place ASCII-only lowercasing.
///
/// POSIX replacement for the Win32 CharLower() call sites: only the bytes
/// 'A'..'Z' are touched, so multi-byte sequences (UTF-8 on POSIX platforms)
/// pass through unharmed. On Windows the original CharLower() is kept for its
/// exact DBCS-aware legacy behavior.
inline void StrLowerAscii(std::string& text)
{
	for (char& ch : text)
	{
		if (ch >= 'A' && ch <= 'Z')
			ch += 'a' - 'A';
	}
}

#endif // PLATFORM_PLATFORMSTRING_H
