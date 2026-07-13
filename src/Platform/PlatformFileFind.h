#ifndef PLATFORM_PLATFORMFILEFIND_H
#define PLATFORM_PLATFORMFILEFIND_H

#pragma once

// Minimal POSIX stand-in for the MSVCRT _findfirst/_findnext directory scan the
// client uses to preload character resources. Only the pattern forms the engine
// passes are supported ("*", "*.*", and "*.ext" with a case-insensitive
// extension), matched against the current working directory, and only the
// `name` field is populated (all the call sites read).

#ifndef _WIN32

#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

struct _finddata_t
{
	unsigned attrib;
	long time_create;
	long time_access;
	long time_write;
	long size;
	char name[260];
};

namespace platform_find_detail
{
struct FindState
{
	std::vector<std::string> names;
	size_t index = 0;
};

inline bool MatchesSpec(const std::string& filename, const std::string& spec)
{
	if (spec == "*" || spec == "*.*")
		return true;

	// "*.ext" form: compare the trailing bytes case-insensitively.
	if (spec.size() >= 2 && spec[0] == '*' && spec[1] == '.')
	{
		const std::string ext = spec.substr(1); // ".ext"
		if (filename.size() < ext.size())
			return false;
		const std::string tail = filename.substr(filename.size() - ext.size());
		for (size_t i = 0; i < ext.size(); ++i)
		{
			if (std::tolower((unsigned char) tail[i]) != std::tolower((unsigned char) ext[i]))
				return false;
		}
		return true;
	}

	return filename == spec;
}

inline void CopyName(char (&dst)[260], const std::string& src)
{
	std::strncpy(dst, src.c_str(), sizeof(dst) - 1);
	dst[sizeof(dst) - 1] = '\0';
}
} // namespace platform_find_detail

// Returns an opaque handle (heap pointer) on success, or 0 when nothing matches.
// NOTE: 0 (not -1) is used for "not found" so the client's `if (handle)` checks
// behave correctly; a valid handle is always a non-null heap pointer.
inline intptr_t _findfirst(const char* spec, _finddata_t* fi)
{
	if (spec == nullptr || fi == nullptr)
		return 0;

	auto* st = new platform_find_detail::FindState();

	std::error_code ec;
	const std::filesystem::path cwd = std::filesystem::current_path(ec);
	if (!ec)
	{
		for (const auto& entry : std::filesystem::directory_iterator(cwd, ec))
		{
			if (ec)
				break;
			if (!entry.is_regular_file(ec))
				continue;
			const std::string name = entry.path().filename().string();
			if (platform_find_detail::MatchesSpec(name, spec))
				st->names.push_back(name);
		}
	}

	if (st->names.empty())
	{
		delete st;
		return 0;
	}

	platform_find_detail::CopyName(fi->name, st->names[0]);
	st->index = 1;
	return reinterpret_cast<intptr_t>(st);
}

inline int _findnext(intptr_t handle, _finddata_t* fi)
{
	auto* st = reinterpret_cast<platform_find_detail::FindState*>(handle);
	if (st == nullptr || fi == nullptr || st->index >= st->names.size())
		return -1;

	platform_find_detail::CopyName(fi->name, st->names[st->index]);
	++st->index;
	return 0;
}

inline int _findclose(intptr_t handle)
{
	delete reinterpret_cast<platform_find_detail::FindState*>(handle);
	return 0;
}

#endif // !_WIN32

#endif // PLATFORM_PLATFORMFILEFIND_H
