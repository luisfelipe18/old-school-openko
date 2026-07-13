#ifndef PLATFORM_PLATFORMINI_H
#define PLATFORM_PLATFORMINI_H

#pragma once

// POSIX replacements for the Win32 private-profile (.ini) readers the client
// uses to load its server list (Server.Ini). Header-only so the Windows
// MSBuild projects keep using the real API and only POSIX builds see these.

#ifndef _WIN32

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "PlatformTypes.h" // DWORD

namespace platform_ini_detail
{
// Trim ASCII whitespace from both ends.
inline std::string Trim(const std::string& s)
{
	size_t b = s.find_first_not_of(" \t\r\n");
	if (b == std::string::npos)
		return {};
	size_t e = s.find_last_not_of(" \t\r\n");
	return s.substr(b, e - b + 1);
}

inline bool IEquals(const std::string& a, const std::string& b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
			return false;
	}
	return true;
}

// Look up [section] key in the given .ini file. Returns true and sets `out`
// when found. Matching is case-insensitive on section and key, mirroring the
// Win32 profile API.
inline bool Lookup(const char* szSection, const char* szKey, const char* szFile, std::string& out)
{
	if (szSection == nullptr || szKey == nullptr || szFile == nullptr)
		return false;

	std::ifstream in(szFile);
	if (!in.is_open())
		return false;

	std::string wantSection = szSection;
	std::string wantKey     = szKey;
	bool inSection          = false;
	std::string line;

	while (std::getline(in, line))
	{
		std::string t = Trim(line);
		if (t.empty() || t[0] == ';' || t[0] == '#')
			continue;

		if (t.front() == '[' && t.back() == ']')
		{
			std::string sec = Trim(t.substr(1, t.size() - 2));
			inSection       = IEquals(sec, wantSection);
			continue;
		}

		if (!inSection)
			continue;

		size_t eq = t.find('=');
		if (eq == std::string::npos)
			continue;

		std::string k = Trim(t.substr(0, eq));
		if (IEquals(k, wantKey))
		{
			out = Trim(t.substr(eq + 1));
			return true;
		}
	}

	return false;
}
} // namespace platform_ini_detail

/// Win32 GetPrivateProfileStringA shim. Copies the value (or the default when
/// the key is absent) into szReturn, truncated to nSize including the null.
/// Returns the number of characters copied (excluding the null).
inline DWORD GetPrivateProfileString(const char* szSection, const char* szKey, const char* szDefault,
	char* szReturn, DWORD nSize, const char* szFile)
{
	if (szReturn == nullptr || nSize == 0)
		return 0;

	std::string value;
	if (!platform_ini_detail::Lookup(szSection, szKey, szFile, value))
		value = (szDefault != nullptr) ? szDefault : "";

	DWORD n = static_cast<DWORD>(value.size());
	if (n > nSize - 1)
		n = nSize - 1;
	std::memcpy(szReturn, value.data(), n);
	szReturn[n] = '\0';
	return n;
}

/// Win32 GetPrivateProfileIntA shim. Returns the integer value of the key, or
/// nDefault when the key is missing or not a valid number.
inline int GetPrivateProfileInt(const char* szSection, const char* szKey, int nDefault, const char* szFile)
{
	std::string value;
	if (!platform_ini_detail::Lookup(szSection, szKey, szFile, value) || value.empty())
		return nDefault;

	char* end     = nullptr;
	long parsed   = std::strtol(value.c_str(), &end, 10);
	if (end == value.c_str())
		return nDefault;
	return static_cast<int>(parsed);
}

/// Win32 WritePrivateProfileStringA shim. Updates szKey in place if it
/// already exists under [szSection] (preserving the rest of the file - other
/// sections, comments, key order), otherwise appends it to the section
/// (creating the section at end-of-file if it doesn't exist yet). Returns
/// false only on I/O failure.
inline bool WritePrivateProfileString(
	const char* szSection, const char* szKey, const char* szValue, const char* szFile)
{
	if (szSection == nullptr || szKey == nullptr || szFile == nullptr)
		return false;

	std::vector<std::string> lines;
	{
		std::ifstream in(szFile);
		std::string line;
		while (std::getline(in, line))
			lines.push_back(line);
	}

	const std::string wantSection = szSection;
	const std::string wantKey     = szKey;
	const std::string newLine     = wantKey + "=" + (szValue != nullptr ? szValue : "");

	bool inSection    = false;
	bool sectionFound = false;
	int insertAt      = -1; // >=0: insert newLine here; -2: updated an existing line in place

	for (size_t i = 0; i < lines.size(); ++i)
	{
		const std::string t = platform_ini_detail::Trim(lines[i]);

		if (!t.empty() && t.front() == '[' && t.back() == ']')
		{
			if (inSection)
			{
				// Leaving our section (another section starts here) without
				// having found the key - insert just before it.
				insertAt = static_cast<int>(i);
				break;
			}
			const std::string sec = platform_ini_detail::Trim(t.substr(1, t.size() - 2));
			inSection             = platform_ini_detail::IEquals(sec, wantSection);
			sectionFound          = sectionFound || inSection;
			continue;
		}

		if (!inSection)
			continue;

		const size_t eq = t.find('=');
		if (eq == std::string::npos)
			continue;

		const std::string k = platform_ini_detail::Trim(t.substr(0, eq));
		if (platform_ini_detail::IEquals(k, wantKey))
		{
			lines[i]  = newLine;
			insertAt  = -2;
			break;
		}
	}

	if (insertAt == -1 && inSection)
		insertAt = static_cast<int>(lines.size()); // our section ran to end-of-file

	if (insertAt >= 0)
	{
		lines.insert(lines.begin() + insertAt, newLine);
	}
	else if (!sectionFound)
	{
		if (!lines.empty() && !lines.back().empty())
			lines.push_back("");
		lines.push_back("[" + wantSection + "]");
		lines.push_back(newLine);
	}

	std::ofstream out(szFile, std::ios::trunc);
	if (!out.is_open())
		return false;
	for (const std::string& l : lines)
		out << l << "\n";
	return true;
}

#endif // !_WIN32

#endif // PLATFORM_PLATFORMINI_H
