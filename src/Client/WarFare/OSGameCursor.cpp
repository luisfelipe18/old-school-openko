#include "StdAfx.h"

#ifndef _WIN32
#include "OSGameCursor.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>

namespace
{
	std::array<SDL_Cursor*, CURSOR_COUNT> g_cursors{};
	bool     g_locked  = false;
	e_Cursor g_current = CURSOR_UNKNOWN;
	e_Cursor g_prev    = CURSOR_UNKNOWN;

	void Apply(e_Cursor which)
	{
		// CURSOR_UNKNOWN (mouse-look hides the pointer on Windows) leaves the
		// live cursor untouched here; the relative-mouse path handles hiding.
		if (which < 0 || which >= CURSOR_COUNT)
			return;
		SDL_Cursor* cursor = g_cursors[static_cast<std::size_t>(which)];
		if (cursor != nullptr)
			SDL_SetCursor(cursor);
	}
}

void OSGameCursor::Register(e_Cursor which, SDL_Cursor* cursor)
{
	if (which < 0 || which >= CURSOR_COUNT)
		return;
	g_cursors[static_cast<std::size_t>(which)] = cursor;
}

void OSGameCursor::Set(e_Cursor which, bool bLocked)
{
	// Mirrors CGameCursor::SetGameCursor so OS-cursor and software-cursor modes
	// lock identically.
	if (g_locked && !bLocked)
		return;

	if ((g_locked && bLocked) || (!g_locked && !bLocked))
	{
		Apply(which);
		g_current = which;
		return;
	}

	// (!g_locked && bLocked): take the lock, remembering what to restore to.
	g_prev    = g_current;
	g_locked  = true;
	Apply(which);
	g_current = which;
}

void OSGameCursor::Restore()
{
	if (g_locked)
		g_locked = false;
	Apply(g_prev);
	g_current = g_prev;
}

void OSGameCursor::DestroyAll()
{
	for (SDL_Cursor*& cursor : g_cursors)
	{
		if (cursor != nullptr)
		{
			SDL_DestroyCursor(cursor);
			cursor = nullptr;
		}
	}
	g_locked  = false;
	g_current = CURSOR_UNKNOWN;
	g_prev    = CURSOR_UNKNOWN;
}
#endif // !_WIN32
