#ifndef PLATFORM_DINPUTKEYCODES_H
#define PLATFORM_DINPUTKEYCODES_H

#pragma once

// DirectInput keyboard scancodes (DIK_*) for POSIX platforms.
//
// The game logic identifies keys by these values everywhere (hotkey tables in
// GameDef.h, CLocalInput queries), so the POSIX port keeps them as the
// engine's canonical key codes and maps SDL scancodes onto them in the input
// phase (docs/PORT_POSIX_PLAN.md, phase 3). Values match <dinput.h> exactly.
//
// On Windows the real <dinput.h> is used; this header defines nothing there.

#ifndef _WIN32

inline constexpr int DIK_ESCAPE       = 0x01;
inline constexpr int DIK_1            = 0x02;
inline constexpr int DIK_2            = 0x03;
inline constexpr int DIK_3            = 0x04;
inline constexpr int DIK_4            = 0x05;
inline constexpr int DIK_5            = 0x06;
inline constexpr int DIK_6            = 0x07;
inline constexpr int DIK_7            = 0x08;
inline constexpr int DIK_8            = 0x09;
inline constexpr int DIK_9            = 0x0A;
inline constexpr int DIK_0            = 0x0B;
inline constexpr int DIK_MINUS        = 0x0C;
inline constexpr int DIK_EQUALS       = 0x0D;
inline constexpr int DIK_BACK         = 0x0E;
inline constexpr int DIK_TAB          = 0x0F;
inline constexpr int DIK_Q            = 0x10;
inline constexpr int DIK_W            = 0x11;
inline constexpr int DIK_E            = 0x12;
inline constexpr int DIK_R            = 0x13;
inline constexpr int DIK_T            = 0x14;
inline constexpr int DIK_Y            = 0x15;
inline constexpr int DIK_U            = 0x16;
inline constexpr int DIK_I            = 0x17;
inline constexpr int DIK_O            = 0x18;
inline constexpr int DIK_P            = 0x19;
inline constexpr int DIK_LBRACKET     = 0x1A;
inline constexpr int DIK_RBRACKET     = 0x1B;
inline constexpr int DIK_RETURN       = 0x1C;
inline constexpr int DIK_LCONTROL     = 0x1D;
inline constexpr int DIK_A            = 0x1E;
inline constexpr int DIK_S            = 0x1F;
inline constexpr int DIK_D            = 0x20;
inline constexpr int DIK_F            = 0x21;
inline constexpr int DIK_G            = 0x22;
inline constexpr int DIK_H            = 0x23;
inline constexpr int DIK_J            = 0x24;
inline constexpr int DIK_K            = 0x25;
inline constexpr int DIK_L            = 0x26;
inline constexpr int DIK_SEMICOLON    = 0x27;
inline constexpr int DIK_APOSTROPHE   = 0x28;
inline constexpr int DIK_GRAVE        = 0x29;
inline constexpr int DIK_LSHIFT       = 0x2A;
inline constexpr int DIK_BACKSLASH    = 0x2B;
inline constexpr int DIK_Z            = 0x2C;
inline constexpr int DIK_X            = 0x2D;
inline constexpr int DIK_C            = 0x2E;
inline constexpr int DIK_V            = 0x2F;
inline constexpr int DIK_B            = 0x30;
inline constexpr int DIK_N            = 0x31;
inline constexpr int DIK_M            = 0x32;
inline constexpr int DIK_COMMA        = 0x33;
inline constexpr int DIK_PERIOD       = 0x34;
inline constexpr int DIK_SLASH        = 0x35;
inline constexpr int DIK_RSHIFT       = 0x36;
inline constexpr int DIK_MULTIPLY     = 0x37;
inline constexpr int DIK_LMENU        = 0x38;
inline constexpr int DIK_SPACE        = 0x39;
inline constexpr int DIK_CAPITAL      = 0x3A;
inline constexpr int DIK_F1           = 0x3B;
inline constexpr int DIK_F2           = 0x3C;
inline constexpr int DIK_F3           = 0x3D;
inline constexpr int DIK_F4           = 0x3E;
inline constexpr int DIK_F5           = 0x3F;
inline constexpr int DIK_F6           = 0x40;
inline constexpr int DIK_F7           = 0x41;
inline constexpr int DIK_F8           = 0x42;
inline constexpr int DIK_F9           = 0x43;
inline constexpr int DIK_F10          = 0x44;
inline constexpr int DIK_NUMLOCK      = 0x45;
inline constexpr int DIK_SCROLL       = 0x46;
inline constexpr int DIK_NUMPAD7      = 0x47;
inline constexpr int DIK_NUMPAD8      = 0x48;
inline constexpr int DIK_NUMPAD9      = 0x49;
inline constexpr int DIK_SUBTRACT     = 0x4A;
inline constexpr int DIK_NUMPAD4      = 0x4B;
inline constexpr int DIK_NUMPAD5      = 0x4C;
inline constexpr int DIK_NUMPAD6      = 0x4D;
inline constexpr int DIK_ADD          = 0x4E;
inline constexpr int DIK_NUMPAD1      = 0x4F;
inline constexpr int DIK_NUMPAD2      = 0x50;
inline constexpr int DIK_NUMPAD3      = 0x51;
inline constexpr int DIK_NUMPAD0      = 0x52;
inline constexpr int DIK_DECIMAL      = 0x53;
inline constexpr int DIK_OEM_102      = 0x56;
inline constexpr int DIK_F11          = 0x57;
inline constexpr int DIK_F12          = 0x58;
inline constexpr int DIK_F13          = 0x64;
inline constexpr int DIK_F14          = 0x65;
inline constexpr int DIK_F15          = 0x66;
inline constexpr int DIK_KANA         = 0x70;
inline constexpr int DIK_ABNT_C1      = 0x73;
inline constexpr int DIK_CONVERT      = 0x79;
inline constexpr int DIK_NOCONVERT    = 0x7B;
inline constexpr int DIK_YEN          = 0x7D;
inline constexpr int DIK_ABNT_C2      = 0x7E;
inline constexpr int DIK_NUMPADEQUALS = 0x8D;
inline constexpr int DIK_PREVTRACK    = 0x90;
inline constexpr int DIK_AT           = 0x91;
inline constexpr int DIK_COLON        = 0x92;
inline constexpr int DIK_UNDERLINE    = 0x93;
inline constexpr int DIK_KANJI        = 0x94;
inline constexpr int DIK_STOP         = 0x95;
inline constexpr int DIK_AX           = 0x96;
inline constexpr int DIK_UNLABELED    = 0x97;
inline constexpr int DIK_NEXTTRACK    = 0x99;
inline constexpr int DIK_NUMPADENTER  = 0x9C;
inline constexpr int DIK_RCONTROL     = 0x9D;
inline constexpr int DIK_MUTE         = 0xA0;
inline constexpr int DIK_CALCULATOR   = 0xA1;
inline constexpr int DIK_PLAYPAUSE    = 0xA2;
inline constexpr int DIK_MEDIASTOP    = 0xA4;
inline constexpr int DIK_VOLUMEDOWN   = 0xAE;
inline constexpr int DIK_VOLUMEUP     = 0xB0;
inline constexpr int DIK_WEBHOME      = 0xB2;
inline constexpr int DIK_NUMPADCOMMA  = 0xB3;
inline constexpr int DIK_DIVIDE       = 0xB5;
inline constexpr int DIK_SYSRQ        = 0xB7;
inline constexpr int DIK_RMENU        = 0xB8;
inline constexpr int DIK_PAUSE        = 0xC5;
inline constexpr int DIK_HOME         = 0xC7;
inline constexpr int DIK_UP           = 0xC8;
inline constexpr int DIK_PRIOR        = 0xC9;
inline constexpr int DIK_LEFT         = 0xCB;
inline constexpr int DIK_RIGHT        = 0xCD;
inline constexpr int DIK_END          = 0xCF;
inline constexpr int DIK_DOWN         = 0xD0;
inline constexpr int DIK_NEXT         = 0xD1;
inline constexpr int DIK_INSERT       = 0xD2;
inline constexpr int DIK_DELETE       = 0xD3;
inline constexpr int DIK_LWIN         = 0xDB;
inline constexpr int DIK_RWIN         = 0xDC;
inline constexpr int DIK_APPS         = 0xDD;
inline constexpr int DIK_POWER        = 0xDE;
inline constexpr int DIK_SLEEP        = 0xDF;
inline constexpr int DIK_WAKE         = 0xE3;
inline constexpr int DIK_WEBSEARCH    = 0xE5;
inline constexpr int DIK_WEBFAVORITES = 0xE6;
inline constexpr int DIK_WEBREFRESH   = 0xE7;
inline constexpr int DIK_WEBSTOP      = 0xE8;
inline constexpr int DIK_WEBFORWARD   = 0xE9;
inline constexpr int DIK_WEBBACK      = 0xEA;
inline constexpr int DIK_MYCOMPUTER   = 0xEB;
inline constexpr int DIK_MAIL         = 0xEC;
inline constexpr int DIK_MEDIASELECT  = 0xED;

// Aliases kept by dinput.h for backwards compatibility.
inline constexpr int DIK_BACKSPACE    = DIK_BACK;
inline constexpr int DIK_NUMPADSTAR   = DIK_MULTIPLY;
inline constexpr int DIK_LALT         = DIK_LMENU;
inline constexpr int DIK_CAPSLOCK     = DIK_CAPITAL;
inline constexpr int DIK_NUMPADMINUS  = DIK_SUBTRACT;
inline constexpr int DIK_NUMPADPLUS   = DIK_ADD;
inline constexpr int DIK_NUMPADPERIOD = DIK_DECIMAL;
inline constexpr int DIK_NUMPADSLASH  = DIK_DIVIDE;
inline constexpr int DIK_RALT         = DIK_RMENU;
inline constexpr int DIK_UPARROW      = DIK_UP;
inline constexpr int DIK_PGUP         = DIK_PRIOR;
inline constexpr int DIK_LEFTARROW    = DIK_LEFT;
inline constexpr int DIK_RIGHTARROW   = DIK_RIGHT;
inline constexpr int DIK_DOWNARROW    = DIK_DOWN;
inline constexpr int DIK_PGDN         = DIK_NEXT;

#endif // !_WIN32

#endif // PLATFORM_DINPUTKEYCODES_H
