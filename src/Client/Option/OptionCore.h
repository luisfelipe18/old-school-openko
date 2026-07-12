#ifndef CLIENT_OPTION_OPTIONCORE_H
#define CLIENT_OPTION_OPTIONCORE_H

#pragma once

// Platform-neutral logic behind the POSIX Option tool (docs/PORT_POSIX_PLAN.md):
// the Option.ini / Server.Ini field set COptionDlg (Windows/MFC) reads and
// writes, kept separate from SDL/Dear ImGui so it's unit-testable without a
// display. Every default and section/key name here matches
// src/Client/Option/OptionDlg.cpp exactly, so an Option.ini written by either
// tool is interchangeable.

#include <cstdint>
#include <string>
#include <vector>

namespace option_core
{

struct GameOption
{
	int iUseShadow      = 1;    // Shadows: 0 off, 1 on
	int iTexLOD_Chr     = 0;    // Character texture LOD: 0 high, 1 low
	int iTexLOD_Shape   = 0;    // Object/shape texture LOD: 0 high, 1 low
	int iTexLOD_Terrain = 0;    // Terrain texture LOD: 0 high, 1 low
	int iViewDist       = 512;  // View distance
	int iViewWidth      = 1024;
	int iViewHeight     = 768;
	int iViewColorDepth = 16;
	int iEffectSndDist  = 48; // Effect sound distance
	int iEffectCount    = 2000;
	bool bSndDuplicated = true; // Allow duplicate sounds to play
	bool bSoundBgm      = true;
	bool bSoundEffect   = true;
	bool bWindowCursor  = true; // Use the software cursor instead of the OS one
	bool bWindowMode    = false;
	bool bEffectVisible = true;
};

struct Resolution
{
	uint32_t Width  = 0;
	uint32_t Height = 0;

	bool operator==(const Resolution& other) const
	{
		return Width == other.Width && Height == other.Height;
	}
};

// The game supports at minimum a resolution of 1024x768; the UIs don't fit
// on anything smaller.
inline constexpr Resolution MIN_RESOLUTION = { 1024, 768 };

// Hardcoded fallback list (matches OptionDlg.cpp's DefaultResolutions),
// sorted descending by width then height, used when live display
// enumeration finds nothing.
std::vector<Resolution> DefaultResolutions();

// Sorts and de-duplicates a raw list of detected display modes the same way
// COptionDlg::LoadSupportedResolutions does: filters anything smaller than
// MIN_RESOLUTION, de-duplicates, sorts descending, falls back to
// DefaultResolutions() if the input is empty after filtering.
std::vector<Resolution> BuildSupportedResolutions(const std::vector<Resolution>& detected);

// Reads every key COptionDlg::SettingLoad reads on Windows, same defaults.
GameOption LoadOptions(const std::string& szIniFile);

// Writes every key COptionDlg::SettingSave writes on Windows, same
// section/key names and value formatting ("%d" for ints, "1"/"0" for bools).
void SaveOptions(const std::string& szIniFile, const GameOption& option);

// Server.Ini [Version] Files - equivalent of the version field COptionDlg
// shows in OnInitDialog and writes in OnBVersion.
int ReadServerVersion(const std::string& szServerIniFile);
void WriteServerVersion(const std::string& szServerIniFile, int iVersion);

} // namespace option_core

#endif // CLIENT_OPTION_OPTIONCORE_H
