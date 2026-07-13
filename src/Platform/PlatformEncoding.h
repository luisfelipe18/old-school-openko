#ifndef PLATFORM_PLATFORMENCODING_H
#define PLATFORM_PLATFORMENCODING_H

#pragma once

#include <string>
#include <string_view>

// Text-encoding boundary helpers (docs/PORT_POSIX_PLAN.md, phase 1).
//
// The game's assets (.tbl strings, UI text) and the client<->server protocol
// carry Korean text in the CP949 (Unified Hangul / EUC-KR superset) codepage.
// The POSIX port keeps UTF-8 in memory and converts at those boundaries.
//
// Implemented with WideCharToMultiByte/MultiByteToWideChar on Windows and
// iconv elsewhere. Unconvertible byte sequences are skipped rather than
// failing the whole string, so a stray corrupt character in a legacy asset
// cannot take down text rendering.

/// \brief Converts CP949 (EUC-KR/UHC) encoded text to UTF-8.
std::string Cp949ToUtf8(std::string_view cp949Text);

/// \brief Converts UTF-8 encoded text to CP949 (EUC-KR/UHC).
std::string Utf8ToCp949(std::string_view utf8Text);

#endif // PLATFORM_PLATFORMENCODING_H
