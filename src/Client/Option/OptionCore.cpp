#include "OptionCore.h"

#include <Platform/PlatformIni.h>

#include <algorithm>
#include <set>

namespace option_core
{

std::vector<Resolution> DefaultResolutions()
{
	return {
		{ 1024, 768 }, { 1152, 864 }, { 1280, 768 },  //
		{ 1280, 800 }, { 1280, 960 }, { 1280, 1024 }, //
		{ 1360, 768 }, { 1366, 768 }, { 1600, 1200 }  //
	};
}

namespace
{
bool DescendingByWidthThenHeight(const Resolution& a, const Resolution& b)
{
	return a.Width > b.Width || (a.Width == b.Width && a.Height > b.Height);
}
} // namespace

std::vector<Resolution> BuildSupportedResolutions(const std::vector<Resolution>& detected)
{
	std::set<Resolution, decltype(&DescendingByWidthThenHeight)> unique(DescendingByWidthThenHeight);

	for (const Resolution& resolution : detected)
	{
		if (resolution.Width < MIN_RESOLUTION.Width || resolution.Height < MIN_RESOLUTION.Height)
			continue;
		unique.insert(resolution);
	}

	std::vector<Resolution> result(unique.begin(), unique.end());
	if (result.empty())
		result = DefaultResolutions();

	std::sort(result.begin(), result.end(), DescendingByWidthThenHeight);
	return result;
}

GameOption LoadOptions(const std::string& szIniFile)
{
	GameOption option;
	const char* szFile       = szIniFile.c_str();

	option.iTexLOD_Chr       = GetPrivateProfileInt("Texture", "LOD_Chr", 0, szFile);
	option.iTexLOD_Shape     = GetPrivateProfileInt("Texture", "LOD_Shape", 0, szFile);
	option.iTexLOD_Terrain   = GetPrivateProfileInt("Texture", "LOD_Terrain", 0, szFile);
	option.iUseShadow        = GetPrivateProfileInt("Shadow", "Use", 1, szFile);
	option.iViewWidth        = GetPrivateProfileInt("ViewPort", "Width", 1024, szFile);
	option.iViewHeight       = GetPrivateProfileInt("ViewPort", "Height", 768, szFile);
	option.iViewColorDepth   = GetPrivateProfileInt("ViewPort", "ColorDepth", 16, szFile);
	option.iViewDist         = GetPrivateProfileInt("ViewPort", "Distance", 512, szFile);
	option.iEffectSndDist    = GetPrivateProfileInt("Sound", "Distance", 48, szFile);
	option.iEffectCount      = GetPrivateProfileInt("Effect", "Count", 2000, szFile);

	option.bSoundBgm         = GetPrivateProfileInt("Sound", "Bgm", 1, szFile) != 0;
	option.bSoundEffect      = GetPrivateProfileInt("Sound", "Effect", 1, szFile) != 0;
	option.bSndDuplicated    = GetPrivateProfileInt("Sound", "Duplicate", 0, szFile) != 0;
	option.bWindowCursor     = GetPrivateProfileInt("Cursor", "WindowCursor", 1, szFile) != 0;
	option.bWindowMode       = GetPrivateProfileInt("Screen", "WindowMode", 0, szFile) != 0;
	option.bEffectVisible    = GetPrivateProfileInt("WeaponEffect", "EffectVisible", 1, szFile) != 0;

	return option;
}

void SaveOptions(const std::string& szIniFile, const GameOption& option)
{
	const char* szFile = szIniFile.c_str();

	WritePrivateProfileString("Texture", "LOD_Chr", std::to_string(option.iTexLOD_Chr).c_str(), szFile);
	WritePrivateProfileString(
		"Texture", "LOD_Shape", std::to_string(option.iTexLOD_Shape).c_str(), szFile);
	WritePrivateProfileString(
		"Texture", "LOD_Terrain", std::to_string(option.iTexLOD_Terrain).c_str(), szFile);
	WritePrivateProfileString("Shadow", "Use", std::to_string(option.iUseShadow).c_str(), szFile);
	WritePrivateProfileString("ViewPort", "Width", std::to_string(option.iViewWidth).c_str(), szFile);
	WritePrivateProfileString("ViewPort", "Height", std::to_string(option.iViewHeight).c_str(), szFile);
	WritePrivateProfileString(
		"ViewPort", "ColorDepth", std::to_string(option.iViewColorDepth).c_str(), szFile);
	WritePrivateProfileString("ViewPort", "Distance", std::to_string(option.iViewDist).c_str(), szFile);
	WritePrivateProfileString(
		"Sound", "Distance", std::to_string(option.iEffectSndDist).c_str(), szFile);
	WritePrivateProfileString("Effect", "Count", std::to_string(option.iEffectCount).c_str(), szFile);

	WritePrivateProfileString("Sound", "Bgm", option.bSoundBgm ? "1" : "0", szFile);
	WritePrivateProfileString("Sound", "Effect", option.bSoundEffect ? "1" : "0", szFile);
	WritePrivateProfileString("Sound", "Duplicate", option.bSndDuplicated ? "1" : "0", szFile);
	WritePrivateProfileString("Cursor", "WindowCursor", option.bWindowCursor ? "1" : "0", szFile);
	WritePrivateProfileString("Screen", "WindowMode", option.bWindowMode ? "1" : "0", szFile);
	WritePrivateProfileString(
		"WeaponEffect", "EffectVisible", option.bEffectVisible ? "1" : "0", szFile);
}

int ReadServerVersion(const std::string& szServerIniFile)
{
	return GetPrivateProfileInt("Version", "Files", 0, szServerIniFile.c_str());
}

void WriteServerVersion(const std::string& szServerIniFile, int iVersion)
{
	WritePrivateProfileString(
		"Version", "Files", std::to_string(iVersion).c_str(), szServerIniFile.c_str());
}

} // namespace option_core
