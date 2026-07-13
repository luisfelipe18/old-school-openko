#ifndef CLIENT_WARFARE_LOCALINPUTSDL_H
#define CLIENT_WARFARE_LOCALINPUTSDL_H

#pragma once

// SDL backend helpers for CLocalInput (POSIX platforms).

/// \brief Maps an SDL_Scancode value onto the engine's canonical DirectInput
///        DIK_* code, or 0 when the key has no DirectInput equivalent.
int SdlScancodeToDik(int sdlScancode);

#endif // CLIENT_WARFARE_LOCALINPUTSDL_H
