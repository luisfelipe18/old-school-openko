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
