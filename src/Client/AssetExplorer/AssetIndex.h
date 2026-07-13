#ifndef CLIENT_ASSETEXPLORER_ASSETINDEX_H
#define CLIENT_ASSETEXPLORER_ASSETINDEX_H

#pragma once

// Asset index for the Asset Explorer (docs/ASSET_EXPLORER_PLAN.md, M0).
//
// Walks a game-data directory tree, classifies every file by extension
// (AssetType.h) and keeps a flat list of entries plus fast, allocation-light
// filtering (text search + category mask). Scan touches the filesystem; Filter
// and the counting helpers are pure and unit-tested against a fixture tree
// (tests/AssetExplorer/AssetIndex_test.cpp).

#include "AssetType.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace assetexplorer
{

struct AssetEntry
{
	std::string relativePath;      // path relative to the scan root, '/'-separated
	std::string relativePathLower; // cached lowercase copy for case-insensitive search
	std::string fileName;          // basename with extension
	AssetType type = AssetType::Unknown;
	AssetCategory category = AssetCategory::Other;
	std::uintmax_t sizeBytes = 0;
};

// Number of AssetCategory values (kept in sync with the enum).
inline constexpr std::size_t kCategoryCount = 6;

/// Bit for a category in a filter mask (1 << ordinal).
unsigned CategoryBit(AssetCategory category);

class AssetIndex
{
public:
	/// Recursively scan `root`, classifying each regular file. Files that don't
	/// match a known asset extension are skipped unless `includeUnknown` is true.
	/// Replaces any previous contents. Returns the number of entries indexed.
	/// Filesystem errors on individual entries are skipped, not fatal.
	std::size_t Scan(const std::filesystem::path& root, bool includeUnknown = false);

	const std::vector<AssetEntry>& Entries() const
	{
		return m_entries;
	}

	const std::filesystem::path& Root() const
	{
		return m_root;
	}

	/// Indices into Entries() matching a case-insensitive substring `query`
	/// (empty matches all) and a `categoryMask` (0 matches all; otherwise a
	/// bitwise-OR of CategoryBit()). Results preserve Entries() order.
	std::vector<std::size_t> Filter(std::string_view query, unsigned categoryMask) const;

	/// Per-category entry counts, indexed by AssetCategory ordinal.
	std::array<std::size_t, kCategoryCount> CountsByCategory() const;

private:
	std::filesystem::path m_root;
	std::vector<AssetEntry> m_entries;
};

} // namespace assetexplorer

#endif // CLIENT_ASSETEXPLORER_ASSETINDEX_H
