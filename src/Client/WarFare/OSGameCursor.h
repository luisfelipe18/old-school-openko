#ifndef _WIN32
#pragma once

#include "GameDef.h" // e_Cursor

struct SDL_Cursor;

// POSIX OS-cursor backend.
//
// On Windows the game swaps the hardware cursor with ::SetCursor(hCursor),
// where hCursor is one of the IDC_CURSOR_* .cur resources. SDL exposes no
// equivalent per-request swap through the engine, so this owns one SDL_Cursor
// per logical e_Cursor and applies it whenever CGameProcedure::SetGameCursor
// asks for a change - the POSIX stand-in for ::SetCursor.
//
// It mirrors CGameCursor's lock/restore semantics so a locked cursor (e.g. the
// repair cursor, set with bLocked=true) behaves exactly as it does on Windows
// and in software-cursor mode.
namespace OSGameCursor
{
	// Take ownership of an SDL_Cursor for the given logical cursor. Called once
	// per cursor at startup after the .cur files are decoded. A nullptr slot
	// (a cursor that failed to decode) is left empty: Set() then keeps whatever
	// cursor is already live rather than clearing it to the system default.
	void Register(e_Cursor which, SDL_Cursor* cursor);

	// Request a cursor change, honoring the lock the same way CGameCursor does.
	void Set(e_Cursor which, bool bLocked);

	// Undo a locked Set(), restoring the cursor that was live before the lock.
	void Restore();

	// Free every registered SDL_Cursor. Called during shutdown.
	void DestroyAll();
}
#endif // !_WIN32
