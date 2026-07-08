#ifndef PLATFORM_PLATFORMSTRING_H
#define PLATFORM_PLATFORMSTRING_H

#pragma once

#include <string>

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
