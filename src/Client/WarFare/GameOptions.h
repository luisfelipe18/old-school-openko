#ifndef CLIENT_WARFARE_GAMEOPTIONS_H
#define CLIENT_WARFARE_GAMEOPTIONS_H

#pragma once

/// \brief Registers the working directory with CN3Base and loads Option.ini
///        into CN3Base::s_Options.
///
/// Shared between the Win32 entry point (WinMain) and the SDL one. The file
/// is looked up in the game directory; on POSIX platforms a per-user copy in
/// GetUserConfigDir() takes precedence when present.
void LoadGameOptions();

#endif // CLIENT_WARFARE_GAMEOPTIONS_H
