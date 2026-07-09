#include "StdAfx.h"
#include "GameOptions.h"

#include <shared/Ini.h>

#include <N3Base/N3Base.h>
#include <Platform/PlatformPaths.h>

#include <filesystem>

// Option.ini loading, extracted verbatim from the old WinMain so the Win32
// and SDL entry points share one implementation.
void LoadGameOptions()
{
	// NOTE: get the current directory and make it known to CN3Base
	CN3Base::PathSet(std::filesystem::current_path().string());

	// NOTE: we are anticipating an Options file to exist within this directory
	std::filesystem::path iniPath = std::filesystem::path(CN3Base::PathGet()) / "Option.ini";

#ifndef _WIN32
	// POSIX platforms prefer a per-user copy when one exists
	// (~/Library/Application Support/OpenKO or $XDG_CONFIG_HOME/openko).
	const std::filesystem::path userConfigDir = GetUserConfigDir();
	if (!userConfigDir.empty())
	{
		std::error_code ec;
		if (std::filesystem::exists(userConfigDir / "Option.ini", ec))
			iniPath = userConfigDir / "Option.ini";
	}
#endif

	CIni ini(iniPath.string());

	// NOTE: what is the texture quality?
	CN3Base::s_Options.iTexLOD_Chr = ini.GetInt("Texture", "LOD_Chr", 0);
	if (CN3Base::s_Options.iTexLOD_Chr < 0)
		CN3Base::s_Options.iTexLOD_Chr = 0;
	if (CN3Base::s_Options.iTexLOD_Chr >= 2)
		CN3Base::s_Options.iTexLOD_Chr = 1;

	// NOTE: what is the texture quality?
	CN3Base::s_Options.iTexLOD_Shape = ini.GetInt("Texture", "LOD_Shape", 0);
	if (CN3Base::s_Options.iTexLOD_Shape < 0)
		CN3Base::s_Options.iTexLOD_Shape = 0;
	if (CN3Base::s_Options.iTexLOD_Shape >= 2)
		CN3Base::s_Options.iTexLOD_Shape = 1;

	// NOTE: what is the texture quality?
	CN3Base::s_Options.iTexLOD_Terrain = ini.GetInt("Texture", "LOD_Terrain", 0);
	if (CN3Base::s_Options.iTexLOD_Terrain < 0)
		CN3Base::s_Options.iTexLOD_Terrain = 0;
	if (CN3Base::s_Options.iTexLOD_Terrain >= 2)
		CN3Base::s_Options.iTexLOD_Terrain = 1;

	// NOTE: should we use shadows?
	CN3Base::s_Options.iUseShadow  = ini.GetInt("Shadow", "Use", 1);

	// NOTE: what is the screen resolution?
	CN3Base::s_Options.iViewWidth  = ini.GetInt("ViewPort", "Width", 1024);
	CN3Base::s_Options.iViewHeight = ini.GetInt("ViewPort", "Height", 768);

	if (CN3Base::s_Options.iViewWidth == 1024 || CN3Base::s_Options.iViewWidth == 1366)
		CN3Base::s_Options.iViewHeight = 768;
	else if (CN3Base::s_Options.iViewWidth == 1280)
		CN3Base::s_Options.iViewHeight = 1024;
	else if (CN3Base::s_Options.iViewWidth == 1600)
		CN3Base::s_Options.iViewHeight = 1200;
	else if (CN3Base::s_Options.iViewWidth == 1920)
		CN3Base::s_Options.iViewHeight = 1080;

	// Load the viewport's color depth
	// Officially this defaults to 16-bit, but this isn't as supported these days so we should
	// just default to 32-bit to ensure compatibility with ChangeDisplaySettings().
	CN3Base::s_Options.iViewColorDepth = ini.GetInt("ViewPort", "ColorDepth", 32);
	if (CN3Base::s_Options.iViewColorDepth != 16 && CN3Base::s_Options.iViewColorDepth != 32)
		CN3Base::s_Options.iViewColorDepth = 32;

	// Load the viewport's draw distance
	CN3Base::s_Options.iViewDist = ini.GetInt("ViewPort", "Distance", 512);
	if (CN3Base::s_Options.iViewDist < 256)
		CN3Base::s_Options.iViewDist = 256;
	if (CN3Base::s_Options.iViewDist > 512)
		CN3Base::s_Options.iViewDist = 512;

	// Load the max distance for sound effects
	CN3Base::s_Options.iEffectSndDist = ini.GetInt("Sound", "Distance", 48);
	if (CN3Base::s_Options.iEffectSndDist < 20)
		CN3Base::s_Options.iEffectSndDist = 20;
	if (CN3Base::s_Options.iEffectSndDist > 48)
		CN3Base::s_Options.iEffectSndDist = 48;

	// Load the sound enabled flags
	CN3Base::s_Options.bSndBgmEnable    = ini.GetBool("Sound", "Bgm", true);
	CN3Base::s_Options.bSndEffectEnable = ini.GetBool("Sound", "Effect", true);
	CN3Base::s_Options.bSndEnable       = (CN3Base::s_Options.bSndBgmEnable || CN3Base::s_Options.bSndEffectEnable);

	// Load config to determine if we should we use the Windows cursor
	// If false, will use the software cursor (CGameCursor) instead.
	CN3Base::s_Options.bWindowCursor    = ini.GetBool("Cursor", "WindowCursor", true);

	// Load config to determine if we should run the game windowed or not.
	// true is windowed, false is fullscreen.
	CN3Base::s_Options.bWindowMode      = ini.GetBool("Screen", "WindowMode",
	// In debug builds, if not otherwise configured, we should just prefer to use windowed mode.
#if defined(_DEBUG)
		true
#else
		false
#endif
	);

	// Load unofficial config to determine if we should enable vsync or not
	// This is officially enabled by default.
	CN3Base::s_Options.bVSyncEnabled = ini.GetBool("Screen", "VSyncEnabled", true);

	// POSIX-only: pick the RHI render backend. "GL" uses the OpenGL backend
	// (docs/PORT_POSIX_PLAN.md, T6.5); anything else keeps the headless Null
	// backend, which is what CI smoke runs. Windows ignores this and uses D3D9.
	const std::string szRenderer         = ini.GetString("Screen", "Renderer", "Null");
	CN3Base::s_Options.bPreferGLRenderer = (szRenderer == "GL" || szRenderer == "gl");
}
