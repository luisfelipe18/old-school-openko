#ifndef FILEIO_PATHRESOLVER_H
#define FILEIO_PATHRESOLVER_H

#pragma once

#include <filesystem>

/// \brief Normalizes Windows-style backslash separators to the native ones.
///
/// The game's assets and legacy code build paths with '\\'; on POSIX
/// filesystems those are ordinary filename characters, so they must be
/// rewritten before hitting the filesystem.
std::filesystem::path NormalizePathSeparators(const std::filesystem::path& path);

/// \brief Resolves a path against the filesystem ignoring ASCII case.
///
/// The engine lowercases asset paths while the files on disk may be
/// MixedCase; that is harmless on Windows (case-insensitive filesystems) but
/// breaks on Linux and on case-sensitive APFS/HFS+ volumes. When \p path does
/// not exist as given, each component is matched case-insensitively against
/// the actual directory entries.
///
/// \returns The resolved existing path, or \p path unchanged when no
///          case-insensitive match exists (so the subsequent open fails with
///          the caller's usual error handling).
std::filesystem::path ResolveCaseInsensitivePath(const std::filesystem::path& path);

#endif // FILEIO_PATHRESOLVER_H
