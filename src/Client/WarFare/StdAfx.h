#ifndef CLIENT_WARFARE_STDAFX_H
#define CLIENT_WARFARE_STDAFX_H

#pragma once

#ifdef _WIN32
// winsock2.h must precede any <windows.h> pulled in via the D3D headers below.
#include <winsock2.h>
#endif
#include <N3Base/My_3DStruct.h>

#if !__has_include(<warfare_config.h>)
#error warfare_config.h missing - copy and rename warfare_config.h.default to warfare_config.h
#endif

#include <warfare_config.h>

#endif // CLIENT_WARFARE_STDAFX_H
