#ifndef PLATFORM_PLATFORMTIME_H
#define PLATFORM_PLATFORMTIME_H

#pragma once

#include <chrono>
#include <cstdint>

#ifndef _WIN32
#include <thread>
#endif

// Header-only on purpose: the MSBuild (Windows) projects pick these up with
// no extra library to link, and the CMake targets get them the same way.

namespace detail
{
inline std::chrono::steady_clock::time_point PlatformClockEpoch()
{
	static const std::chrono::steady_clock::time_point epoch = std::chrono::steady_clock::now();
	return epoch;
}
} // namespace detail

/// \brief Monotonic millisecond tick counter.
///
/// Portable replacement for the winmm timeGetTime(): a steadily increasing
/// millisecond count with an arbitrary epoch (process start). Like
/// timeGetTime() it wraps around at the uint32_t limit, so callers must only
/// ever compare differences between ticks - which is how the engine already
/// uses it.
inline uint32_t PlatformTickMs()
{
	const auto elapsed = std::chrono::steady_clock::now() - detail::PlatformClockEpoch();
	return static_cast<uint32_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

/// \brief Monotonic clock in seconds, with an arbitrary epoch.
///
/// Higher-resolution counterpart of PlatformTickMs() for frame timing
/// (replaces the QueryPerformanceCounter path of the legacy DXUtil timer).
inline double PlatformTimeSeconds()
{
	const auto elapsed = std::chrono::steady_clock::now() - detail::PlatformClockEpoch();
	return std::chrono::duration<double>(elapsed).count();
}

#ifndef _WIN32
// Win32 Sleep() shim: block the current thread for the given milliseconds.
inline void Sleep(uint32_t dwMilliseconds)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(dwMilliseconds));
}

// Win32 GetTickCount64() shim: milliseconds since an arbitrary epoch, 64-bit so
// it does not wrap. Callers only ever compare differences.
inline uint64_t GetTickCount64()
{
	const auto elapsed = std::chrono::steady_clock::now() - detail::PlatformClockEpoch();
	return static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}
#endif

#endif // PLATFORM_PLATFORMTIME_H
