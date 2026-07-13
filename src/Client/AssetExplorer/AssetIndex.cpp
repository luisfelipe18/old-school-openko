#include "AssetIndex.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace assetexplorer
{
namespace
{

std::string ToLower(std::string_view s)
{
	std::string out(s);
	std::transform(out.begin(), out.end(), out.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out;
}

// Relative path as a forward-slash string, so search and display are stable
// across platforms regardless of the native separator.
std::string ToGenericRelative(const std::filesystem::path& root, const std::filesystem::path& p)
{
	std::error_code ec;
	std::filesystem::path rel = std::filesystem::relative(p, root, ec);
	if (ec || rel.empty())
		rel = p.filename();
	return rel.generic_string();
}

} // namespace

unsigned CategoryBit(AssetCategory category)
{
	return 1u << static_cast<unsigned>(category);
}

std::size_t AssetIndex::Scan(const std::filesystem::path& root, bool includeUnknown)
{
	m_root = root;
	m_entries.clear();

	std::error_code ec;
	if (!std::filesystem::is_directory(root, ec))
		return 0;

	// skip_permission_denied keeps a single unreadable subtree from aborting
	// the whole walk; per-entry errors are handled by the error_code overloads.
	auto options = std::filesystem::directory_options::skip_permission_denied;
	std::filesystem::recursive_directory_iterator it(root, options, ec), end;
	if (ec)
		return 0;

	for (; it != end; it.increment(ec))
	{
		if (ec)
		{
			ec.clear();
			continue;
		}

		std::error_code fileEc;
		if (!it->is_regular_file(fileEc) || fileEc)
			continue;

		const std::filesystem::path& path = it->path();
		const AssetType type = DetectByExtension(path.generic_string());
		if (type == AssetType::Unknown && !includeUnknown)
			continue;

		AssetEntry entry;
		entry.relativePath = ToGenericRelative(root, path);
		entry.relativePathLower = ToLower(entry.relativePath);
		entry.fileName = path.filename().string();
		entry.type = type;
		entry.category = CategoryOf(type);
		entry.sizeBytes = it->file_size(fileEc);
		if (fileEc)
			entry.sizeBytes = 0;

		m_entries.push_back(std::move(entry));
	}

	std::sort(m_entries.begin(), m_entries.end(),
		[](const AssetEntry& a, const AssetEntry& b) { return a.relativePath < b.relativePath; });

	return m_entries.size();
}

std::vector<std::size_t> AssetIndex::Filter(std::string_view query, unsigned categoryMask) const
{
	const std::string queryLower = ToLower(query);

	std::vector<std::size_t> out;
	out.reserve(m_entries.size());
	for (std::size_t i = 0; i < m_entries.size(); ++i)
	{
		const AssetEntry& e = m_entries[i];

		if (categoryMask != 0 && (categoryMask & CategoryBit(e.category)) == 0)
			continue;

		if (!queryLower.empty()
			&& e.relativePathLower.find(queryLower) == std::string::npos)
			continue;

		out.push_back(i);
	}
	return out;
}

std::array<std::size_t, kCategoryCount> AssetIndex::CountsByCategory() const
{
	std::array<std::size_t, kCategoryCount> counts{};
	for (const AssetEntry& e : m_entries)
	{
		const auto ordinal = static_cast<std::size_t>(e.category);
		if (ordinal < counts.size())
			++counts[ordinal];
	}
	return counts;
}

} // namespace assetexplorer
