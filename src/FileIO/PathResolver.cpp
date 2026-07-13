#include "PathResolver.h"

#include <string>

namespace fs = std::filesystem;

namespace
{
bool EqualsIgnoreCaseAscii(const std::string& lhs, const std::string& rhs)
{
	if (lhs.size() != rhs.size())
		return false;

	for (size_t i = 0; i < lhs.size(); ++i)
	{
		char a = lhs[i];
		char b = rhs[i];

		if (a >= 'A' && a <= 'Z')
			a += 'a' - 'A';
		if (b >= 'A' && b <= 'Z')
			b += 'a' - 'A';

		if (a != b)
			return false;
	}

	return true;
}
} // namespace

fs::path NormalizePathSeparators(const fs::path& path)
{
#ifdef _WIN32
	return path;
#else
	std::string native = path.native();
	for (char& ch : native)
	{
		if (ch == '\\')
			ch = '/';
	}
	return fs::path(native);
#endif
}

fs::path ResolveCaseInsensitivePath(const fs::path& path)
{
	const fs::path normalized = NormalizePathSeparators(path);

	std::error_code ec;
	if (fs::exists(normalized, ec))
		return normalized;

	fs::path result;
	fs::path remainder;
	if (normalized.is_absolute())
	{
		result    = normalized.root_path();
		remainder = normalized.relative_path();
	}
	else
	{
		result    = fs::path(".");
		remainder = normalized;
	}

	for (const fs::path& component : remainder)
	{
		if (component == "." || component == "..")
		{
			result /= component;
			continue;
		}

		fs::path candidate = result / component;
		if (fs::exists(candidate, ec))
		{
			result = std::move(candidate);
			continue;
		}

		// Exact component not present: scan the directory for a
		// case-insensitive match.
		bool found = false;
		for (const fs::directory_entry& entry : fs::directory_iterator(result, ec))
		{
			if (EqualsIgnoreCaseAscii(entry.path().filename().string(), component.string()))
			{
				result = entry.path();
				found  = true;
				break;
			}
		}

		if (!found)
			return normalized; // let the caller's open fail with its usual handling
	}

	return result;
}
