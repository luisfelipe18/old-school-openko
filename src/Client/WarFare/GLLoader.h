#ifndef CLIENT_WARFARE_GLLOADER_H
#define CLIENT_WARFARE_GLLOADER_H

#pragma once

// Minimal OpenGL function loader for the RHI GL backend (docs/PORT_POSIX_PLAN.md,
// T6.5). Rather than vendoring a full generated glad, we declare only the
// functions the backend actually calls and resolve them at runtime through
// SDL_GL_GetProcAddress. Everything lives in namespace gl so nothing collides
// with a system <GL/gl.h> and no build-time OpenGL link is needed - SDL owns
// the context and the loader. The surface grows as later phases (T6.6/T6.7)
// add geometry, textures and shaders.

namespace gl
{
using Enum     = unsigned int;
using Bitfield = unsigned int;
using Int      = int;
using Sizei    = int;
using Float    = float;
using Clampf   = float;
using Clampd   = double;
using Ubyte    = unsigned char;
using Boolean  = unsigned char;

// glClear masks / capabilities / glGetString names (core GL constants).
constexpr Enum COLOR_BUFFER_BIT   = 0x00004000;
constexpr Enum DEPTH_BUFFER_BIT   = 0x00000100;
constexpr Enum STENCIL_BUFFER_BIT = 0x00000400;
constexpr Enum DEPTH_TEST         = 0x0B71;
constexpr Enum SCISSOR_TEST       = 0x0C11;
constexpr Enum VENDOR             = 0x1F00;
constexpr Enum RENDERER           = 0x1F01;
constexpr Enum VERSION            = 0x1F02;

// Loaded entry points (null until Load() succeeds).
extern void (*ClearColor)(Clampf r, Clampf g, Clampf b, Clampf a);
extern void (*ClearDepth)(Clampd depth);
extern void (*Clear)(Bitfield mask);
extern void (*Viewport)(Int x, Int y, Sizei width, Sizei height);
extern void (*Scissor)(Int x, Int y, Sizei width, Sizei height);
extern void (*Enable)(Enum cap);
extern void (*Disable)(Enum cap);
extern Enum (*GetError)();
extern const Ubyte* (*GetString)(Enum name);

// SDL_GL_GetProcAddress has this shape (SDL_FunctionPointer == void(*)()).
using Proc       = void (*)();
using ProcLoader = Proc (*)(const char*);

/// Resolves every entry point above through the loader. Returns false if any
/// required function is missing (the context is then unusable).
bool Load(ProcLoader loader);

} // namespace gl

#endif // CLIENT_WARFARE_GLLOADER_H
