#ifndef CLIENT_WARFARE_GLLOADER_H
#define CLIENT_WARFARE_GLLOADER_H

#pragma once

// Minimal OpenGL function loader for the RHI GL backend (docs/PORT_POSIX_PLAN.md,
// T6.5-T6.7). Rather than vendoring a full generated glad, we declare only the
// functions the backend actually calls and resolve them at runtime through
// SDL_GL_GetProcAddress. Everything lives in namespace gl so nothing collides
// with a system <GL/gl.h> and no build-time OpenGL link is needed - SDL owns
// the context and the loader.

#include <cstdint>

namespace gl
{
using Enum     = unsigned int;
using Bitfield = unsigned int;
using Int      = int;
using Uint     = unsigned int;
using Sizei    = int;
using Sizeiptr = intptr_t;
using Intptr   = intptr_t;
using Float    = float;
using Clampf   = float;
using Clampd   = double;
using Ubyte    = unsigned char;
using Boolean  = unsigned char;
using Char     = char;

// --- Core constants (values from the official GL registry) --------------------

// Clear masks / caps / strings
constexpr Enum COLOR_BUFFER_BIT   = 0x00004000;
constexpr Enum DEPTH_BUFFER_BIT   = 0x00000100;
constexpr Enum STENCIL_BUFFER_BIT = 0x00000400;
constexpr Enum DEPTH_TEST         = 0x0B71;
constexpr Enum SCISSOR_TEST       = 0x0C11;
constexpr Enum BLEND              = 0x0BE2;
constexpr Enum CULL_FACE          = 0x0B44;
constexpr Enum VENDOR             = 0x1F00;
constexpr Enum RENDERER           = 0x1F01;
constexpr Enum VERSION            = 0x1F02;
constexpr Enum EXTENSIONS         = 0x1F03;
constexpr Enum NUM_EXTENSIONS     = 0x821D;
constexpr Enum MAX_TEXTURE_SIZE   = 0x0D33;
constexpr Enum UNPACK_ALIGNMENT   = 0x0CF5;

// Comparison functions
constexpr Enum NEVER    = 0x0200;
constexpr Enum LESS     = 0x0201;
constexpr Enum EQUAL    = 0x0202;
constexpr Enum LEQUAL   = 0x0203;
constexpr Enum GREATER  = 0x0204;
constexpr Enum NOTEQUAL = 0x0205;
constexpr Enum GEQUAL   = 0x0206;
constexpr Enum ALWAYS   = 0x0207;

// Blend factors
constexpr Enum ZERO                = 0;
constexpr Enum ONE                 = 1;
constexpr Enum SRC_COLOR           = 0x0300;
constexpr Enum ONE_MINUS_SRC_COLOR = 0x0301;
constexpr Enum SRC_ALPHA           = 0x0302;
constexpr Enum ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr Enum DST_ALPHA           = 0x0304;
constexpr Enum ONE_MINUS_DST_ALPHA = 0x0305;
constexpr Enum DST_COLOR           = 0x0306;
constexpr Enum ONE_MINUS_DST_COLOR = 0x0307;
constexpr Enum SRC_ALPHA_SATURATE  = 0x0308;

// Faces / winding
constexpr Enum FRONT = 0x0404;
constexpr Enum BACK  = 0x0405;
constexpr Enum CW    = 0x0900;
constexpr Enum CCW   = 0x0901;

// Primitives
constexpr Enum POINTS         = 0x0000;
constexpr Enum LINES          = 0x0001;
constexpr Enum LINE_STRIP     = 0x0003;
constexpr Enum TRIANGLES      = 0x0004;
constexpr Enum TRIANGLE_STRIP = 0x0005;
constexpr Enum TRIANGLE_FAN   = 0x0006;

// Types
constexpr Enum UNSIGNED_BYTE               = 0x1401;
constexpr Enum UNSIGNED_SHORT              = 0x1403;
constexpr Enum UNSIGNED_INT                = 0x1405;
constexpr Enum FLOAT                       = 0x1406;
constexpr Enum UNSIGNED_SHORT_4_4_4_4_REV  = 0x8365;
constexpr Enum UNSIGNED_SHORT_1_5_5_5_REV  = 0x8366;

// Pixel formats
constexpr Enum RGB      = 0x1907;
constexpr Enum RGBA     = 0x1908;
constexpr Enum BGR      = 0x80E0;
constexpr Enum BGRA     = 0x80E1;
constexpr Enum RGBA8    = 0x8058;
constexpr Enum RGB8     = 0x8051;
constexpr Enum RGB5_A1  = 0x8057;
constexpr Enum RGBA4    = 0x8056;
constexpr Enum COMPRESSED_RGB_S3TC_DXT1  = 0x83F0;
constexpr Enum COMPRESSED_RGBA_S3TC_DXT1 = 0x83F1;
constexpr Enum COMPRESSED_RGBA_S3TC_DXT3 = 0x83F2;
constexpr Enum COMPRESSED_RGBA_S3TC_DXT5 = 0x83F3;

// Textures & samplers
constexpr Enum TEXTURE_2D           = 0x0DE1;
constexpr Enum TEXTURE0             = 0x84C0;
constexpr Enum TEXTURE_MAG_FILTER   = 0x2800;
constexpr Enum TEXTURE_MIN_FILTER   = 0x2801;
constexpr Enum TEXTURE_WRAP_S       = 0x2802;
constexpr Enum TEXTURE_WRAP_T       = 0x2803;
constexpr Enum TEXTURE_BORDER_COLOR = 0x1004;
constexpr Enum TEXTURE_MAX_LEVEL    = 0x813D;
constexpr Enum NEAREST                = 0x2600;
constexpr Enum LINEAR                 = 0x2601;
constexpr Enum NEAREST_MIPMAP_NEAREST = 0x2700;
constexpr Enum LINEAR_MIPMAP_NEAREST  = 0x2701;
constexpr Enum NEAREST_MIPMAP_LINEAR  = 0x2702;
constexpr Enum LINEAR_MIPMAP_LINEAR   = 0x2703;
constexpr Enum REPEAT           = 0x2901;
constexpr Enum MIRRORED_REPEAT  = 0x8370;
constexpr Enum CLAMP_TO_EDGE    = 0x812F;
constexpr Enum CLAMP_TO_BORDER  = 0x812D;

// Buffers
constexpr Enum ARRAY_BUFFER         = 0x8892;
constexpr Enum ELEMENT_ARRAY_BUFFER = 0x8893;
constexpr Enum STATIC_DRAW          = 0x88E4;
constexpr Enum STREAM_DRAW          = 0x88E0;
constexpr Enum DYNAMIC_DRAW         = 0x88E8;

// Shaders
constexpr Enum FRAGMENT_SHADER = 0x8B30;
constexpr Enum VERTEX_SHADER   = 0x8B31;
constexpr Enum COMPILE_STATUS  = 0x8B81;
constexpr Enum LINK_STATUS     = 0x8B82;

// --- Loaded entry points (null until Load() succeeds) -------------------------

extern void (*ClearColor)(Clampf r, Clampf g, Clampf b, Clampf a);
extern void (*ClearDepth)(Clampd depth);
extern void (*Clear)(Bitfield mask);
extern void (*Viewport)(Int x, Int y, Sizei width, Sizei height);
extern void (*DepthRange)(Clampd zNear, Clampd zFar);
extern void (*Scissor)(Int x, Int y, Sizei width, Sizei height);
extern void (*Enable)(Enum cap);
extern void (*Disable)(Enum cap);
extern Enum (*GetError)();
extern const Ubyte* (*GetString)(Enum name);
extern const Ubyte* (*GetStringi)(Enum name, Uint index);
extern void (*GetIntegerv)(Enum pname, Int* params);
extern void (*PixelStorei)(Enum pname, Int param);
extern void (*ReadPixels)(
	Int x, Int y, Sizei width, Sizei height, Enum format, Enum type, void* pixels);

// Fixed state
extern void (*BlendFunc)(Enum sfactor, Enum dfactor);
extern void (*DepthFunc)(Enum func);
extern void (*DepthMask)(Boolean flag);
extern void (*CullFace)(Enum mode);
extern void (*FrontFace)(Enum mode);

// Buffers + vertex arrays
extern void (*GenBuffers)(Sizei n, Uint* buffers);
extern void (*DeleteBuffers)(Sizei n, const Uint* buffers);
extern void (*BindBuffer)(Enum target, Uint buffer);
extern void (*BufferData)(Enum target, Sizeiptr size, const void* data, Enum usage);
extern void (*BufferSubData)(Enum target, Intptr offset, Sizeiptr size, const void* data);
extern void (*GenVertexArrays)(Sizei n, Uint* arrays);
extern void (*DeleteVertexArrays)(Sizei n, const Uint* arrays);
extern void (*BindVertexArray)(Uint array);
extern void (*VertexAttribPointer)(
	Uint index, Int size, Enum type, Boolean normalized, Sizei stride, const void* pointer);
extern void (*EnableVertexAttribArray)(Uint index);
extern void (*DisableVertexAttribArray)(Uint index);

// Textures + samplers
extern void (*GenTextures)(Sizei n, Uint* textures);
extern void (*DeleteTextures)(Sizei n, const Uint* textures);
extern void (*BindTexture)(Enum target, Uint texture);
extern void (*ActiveTexture)(Enum texture);
extern void (*TexImage2D)(Enum target, Int level, Int internalformat, Sizei width, Sizei height,
	Int border, Enum format, Enum type, const void* pixels);
extern void (*CompressedTexImage2D)(Enum target, Int level, Enum internalformat, Sizei width,
	Sizei height, Int border, Sizei imageSize, const void* data);
extern void (*TexParameteri)(Enum target, Enum pname, Int param);
extern void (*GenSamplers)(Sizei count, Uint* samplers);
extern void (*DeleteSamplers)(Sizei count, const Uint* samplers);
extern void (*BindSampler)(Uint unit, Uint sampler);
extern void (*SamplerParameteri)(Uint sampler, Enum pname, Int param);
extern void (*SamplerParameterfv)(Uint sampler, Enum pname, const Float* params);

// Shaders + uniforms
extern Uint (*CreateShader)(Enum type);
extern void (*ShaderSource)(Uint shader, Sizei count, const Char* const* string, const Int* length);
extern void (*CompileShader)(Uint shader);
extern void (*GetShaderiv)(Uint shader, Enum pname, Int* params);
extern void (*GetShaderInfoLog)(Uint shader, Sizei bufSize, Sizei* length, Char* infoLog);
extern void (*DeleteShader)(Uint shader);
extern Uint (*CreateProgram)();
extern void (*AttachShader)(Uint program, Uint shader);
extern void (*LinkProgram)(Uint program);
extern void (*GetProgramiv)(Uint program, Enum pname, Int* params);
extern void (*GetProgramInfoLog)(Uint program, Sizei bufSize, Sizei* length, Char* infoLog);
extern void (*UseProgram)(Uint program);
extern void (*DeleteProgram)(Uint program);
extern Int (*GetUniformLocation)(Uint program, const Char* name);
extern void (*Uniform1i)(Int location, Int v0);
extern void (*Uniform1iv)(Int location, Sizei count, const Int* value);
extern void (*Uniform1f)(Int location, Float v0);
extern void (*Uniform2f)(Int location, Float v0, Float v1);
extern void (*Uniform3fv)(Int location, Sizei count, const Float* value);
extern void (*Uniform4fv)(Int location, Sizei count, const Float* value);
extern void (*UniformMatrix4fv)(Int location, Sizei count, Boolean transpose, const Float* value);

// Draws
extern void (*DrawArrays)(Enum mode, Int first, Sizei count);
extern void (*DrawElements)(Enum mode, Sizei count, Enum type, const void* indices);
extern void (*DrawElementsBaseVertex)(
	Enum mode, Sizei count, Enum type, const void* indices, Int basevertex);

// SDL_GL_GetProcAddress has this shape (SDL_FunctionPointer == void(*)()).
using Proc       = void (*)();
using ProcLoader = Proc (*)(const char*);

/// Resolves every entry point above through the loader. Returns false if any
/// required function is missing (the context is then unusable).
bool Load(ProcLoader loader);

} // namespace gl

#endif // CLIENT_WARFARE_GLLOADER_H
