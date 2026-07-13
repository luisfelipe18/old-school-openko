#ifndef CLIENT_WARFARE_GAMEPROCLOGIN_H
#define CLIENT_WARFARE_GAMEPROCLOGIN_H

#pragma once

// Both login scene variants are always compiled in and instantiated
// (CGameProcedure::s_pProcLogIn_1098 / s_pProcLogIn_1298) so the in-scene
// toggle (bottom-left corner, CGameProcedure::ToggleLoginVariant) can switch
// between them at runtime. LOGIN_SCENE_VERSION (warfare_config.h) only picks
// which one is active on startup.
#include "GameProcLogIn_1098.h"
#include "GameProcLogIn_1298.h"

#endif // CLIENT_WARFARE_GAMEPROCLOGIN_H
