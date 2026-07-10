#include "StdAfx.h"
#include "GLLoader.h"

namespace gl
{
void (*ClearColor)(Clampf, Clampf, Clampf, Clampf) = nullptr;
void (*ClearDepth)(Clampd)                         = nullptr;
void (*Clear)(Bitfield)                            = nullptr;
void (*Viewport)(Int, Int, Sizei, Sizei)           = nullptr;
void (*DepthRange)(Clampd, Clampd)                 = nullptr;
void (*Scissor)(Int, Int, Sizei, Sizei)            = nullptr;
void (*Enable)(Enum)                               = nullptr;
void (*Disable)(Enum)                              = nullptr;
Enum (*GetError)()                                 = nullptr;
const Ubyte* (*GetString)(Enum)                    = nullptr;
const Ubyte* (*GetStringi)(Enum, Uint)             = nullptr;
void (*GetIntegerv)(Enum, Int*)                    = nullptr;
void (*PixelStorei)(Enum, Int)                     = nullptr;
void (*ReadPixels)(Int, Int, Sizei, Sizei, Enum, Enum, void*) = nullptr;

void (*BlendFunc)(Enum, Enum) = nullptr;
void (*DepthFunc)(Enum)       = nullptr;
void (*DepthMask)(Boolean)    = nullptr;
void (*CullFace)(Enum)        = nullptr;
void (*FrontFace)(Enum)       = nullptr;

void (*GenBuffers)(Sizei, Uint*)                           = nullptr;
void (*DeleteBuffers)(Sizei, const Uint*)                  = nullptr;
void (*BindBuffer)(Enum, Uint)                             = nullptr;
void (*BufferData)(Enum, Sizeiptr, const void*, Enum)      = nullptr;
void (*BufferSubData)(Enum, Intptr, Sizeiptr, const void*) = nullptr;
void (*GenVertexArrays)(Sizei, Uint*)                      = nullptr;
void (*DeleteVertexArrays)(Sizei, const Uint*)             = nullptr;
void (*BindVertexArray)(Uint)                              = nullptr;
void (*VertexAttribPointer)(Uint, Int, Enum, Boolean, Sizei, const void*) = nullptr;
void (*EnableVertexAttribArray)(Uint)                      = nullptr;
void (*DisableVertexAttribArray)(Uint)                     = nullptr;

void (*GenTextures)(Sizei, Uint*)          = nullptr;
void (*DeleteTextures)(Sizei, const Uint*) = nullptr;
void (*BindTexture)(Enum, Uint)            = nullptr;
void (*ActiveTexture)(Enum)                = nullptr;
void (*TexImage2D)(Enum, Int, Int, Sizei, Sizei, Int, Enum, Enum, const void*)   = nullptr;
void (*CompressedTexImage2D)(Enum, Int, Enum, Sizei, Sizei, Int, Sizei, const void*) = nullptr;
void (*TexParameteri)(Enum, Enum, Int)             = nullptr;
void (*GenSamplers)(Sizei, Uint*)                  = nullptr;
void (*DeleteSamplers)(Sizei, const Uint*)         = nullptr;
void (*BindSampler)(Uint, Uint)                    = nullptr;
void (*SamplerParameteri)(Uint, Enum, Int)         = nullptr;
void (*SamplerParameterfv)(Uint, Enum, const Float*) = nullptr;

Uint (*CreateShader)(Enum)                                        = nullptr;
void (*ShaderSource)(Uint, Sizei, const Char* const*, const Int*) = nullptr;
void (*CompileShader)(Uint)                                       = nullptr;
void (*GetShaderiv)(Uint, Enum, Int*)                             = nullptr;
void (*GetShaderInfoLog)(Uint, Sizei, Sizei*, Char*)              = nullptr;
void (*DeleteShader)(Uint)                                        = nullptr;
Uint (*CreateProgram)()                                           = nullptr;
void (*AttachShader)(Uint, Uint)                                  = nullptr;
void (*LinkProgram)(Uint)                                         = nullptr;
void (*GetProgramiv)(Uint, Enum, Int*)                            = nullptr;
void (*GetProgramInfoLog)(Uint, Sizei, Sizei*, Char*)             = nullptr;
void (*UseProgram)(Uint)                                          = nullptr;
void (*DeleteProgram)(Uint)                                       = nullptr;
Int (*GetUniformLocation)(Uint, const Char*)                      = nullptr;
void (*Uniform1i)(Int, Int)                                       = nullptr;
void (*Uniform1iv)(Int, Sizei, const Int*)                        = nullptr;
void (*Uniform1f)(Int, Float)                                     = nullptr;
void (*Uniform2f)(Int, Float, Float)                              = nullptr;
void (*Uniform3fv)(Int, Sizei, const Float*)                      = nullptr;
void (*Uniform4fv)(Int, Sizei, const Float*)                      = nullptr;
void (*UniformMatrix4fv)(Int, Sizei, Boolean, const Float*)       = nullptr;

void (*DrawArrays)(Enum, Int, Sizei)                              = nullptr;
void (*DrawElements)(Enum, Sizei, Enum, const void*)              = nullptr;
void (*DrawElementsBaseVertex)(Enum, Sizei, Enum, const void*, Int) = nullptr;

namespace
{
// reinterpret_cast between function-pointer types is well-defined for calling
// back through the original type, which is exactly how GL loaders operate.
template <typename Fn>
bool Resolve(Fn& fn, ProcLoader loader, const char* name)
{
	fn = reinterpret_cast<Fn>(loader(name));
	return fn != nullptr;
}
} // namespace

bool Load(ProcLoader loader)
{
	if (loader == nullptr)
		return false;

	bool ok = true;
	ok &= Resolve(ClearColor, loader, "glClearColor");
	ok &= Resolve(ClearDepth, loader, "glClearDepth");
	ok &= Resolve(Clear, loader, "glClear");
	ok &= Resolve(Viewport, loader, "glViewport");
	ok &= Resolve(DepthRange, loader, "glDepthRange");
	ok &= Resolve(Scissor, loader, "glScissor");
	ok &= Resolve(Enable, loader, "glEnable");
	ok &= Resolve(Disable, loader, "glDisable");
	ok &= Resolve(GetError, loader, "glGetError");
	ok &= Resolve(GetString, loader, "glGetString");
	ok &= Resolve(GetStringi, loader, "glGetStringi");
	ok &= Resolve(GetIntegerv, loader, "glGetIntegerv");
	ok &= Resolve(PixelStorei, loader, "glPixelStorei");
	ok &= Resolve(ReadPixels, loader, "glReadPixels");

	ok &= Resolve(BlendFunc, loader, "glBlendFunc");
	ok &= Resolve(DepthFunc, loader, "glDepthFunc");
	ok &= Resolve(DepthMask, loader, "glDepthMask");
	ok &= Resolve(CullFace, loader, "glCullFace");
	ok &= Resolve(FrontFace, loader, "glFrontFace");

	ok &= Resolve(GenBuffers, loader, "glGenBuffers");
	ok &= Resolve(DeleteBuffers, loader, "glDeleteBuffers");
	ok &= Resolve(BindBuffer, loader, "glBindBuffer");
	ok &= Resolve(BufferData, loader, "glBufferData");
	ok &= Resolve(BufferSubData, loader, "glBufferSubData");
	ok &= Resolve(GenVertexArrays, loader, "glGenVertexArrays");
	ok &= Resolve(DeleteVertexArrays, loader, "glDeleteVertexArrays");
	ok &= Resolve(BindVertexArray, loader, "glBindVertexArray");
	ok &= Resolve(VertexAttribPointer, loader, "glVertexAttribPointer");
	ok &= Resolve(EnableVertexAttribArray, loader, "glEnableVertexAttribArray");
	ok &= Resolve(DisableVertexAttribArray, loader, "glDisableVertexAttribArray");

	ok &= Resolve(GenTextures, loader, "glGenTextures");
	ok &= Resolve(DeleteTextures, loader, "glDeleteTextures");
	ok &= Resolve(BindTexture, loader, "glBindTexture");
	ok &= Resolve(ActiveTexture, loader, "glActiveTexture");
	ok &= Resolve(TexImage2D, loader, "glTexImage2D");
	ok &= Resolve(CompressedTexImage2D, loader, "glCompressedTexImage2D");
	ok &= Resolve(TexParameteri, loader, "glTexParameteri");
	ok &= Resolve(GenSamplers, loader, "glGenSamplers");
	ok &= Resolve(DeleteSamplers, loader, "glDeleteSamplers");
	ok &= Resolve(BindSampler, loader, "glBindSampler");
	ok &= Resolve(SamplerParameteri, loader, "glSamplerParameteri");
	ok &= Resolve(SamplerParameterfv, loader, "glSamplerParameterfv");

	ok &= Resolve(CreateShader, loader, "glCreateShader");
	ok &= Resolve(ShaderSource, loader, "glShaderSource");
	ok &= Resolve(CompileShader, loader, "glCompileShader");
	ok &= Resolve(GetShaderiv, loader, "glGetShaderiv");
	ok &= Resolve(GetShaderInfoLog, loader, "glGetShaderInfoLog");
	ok &= Resolve(DeleteShader, loader, "glDeleteShader");
	ok &= Resolve(CreateProgram, loader, "glCreateProgram");
	ok &= Resolve(AttachShader, loader, "glAttachShader");
	ok &= Resolve(LinkProgram, loader, "glLinkProgram");
	ok &= Resolve(GetProgramiv, loader, "glGetProgramiv");
	ok &= Resolve(GetProgramInfoLog, loader, "glGetProgramInfoLog");
	ok &= Resolve(UseProgram, loader, "glUseProgram");
	ok &= Resolve(DeleteProgram, loader, "glDeleteProgram");
	ok &= Resolve(GetUniformLocation, loader, "glGetUniformLocation");
	ok &= Resolve(Uniform1i, loader, "glUniform1i");
	ok &= Resolve(Uniform1iv, loader, "glUniform1iv");
	ok &= Resolve(Uniform1f, loader, "glUniform1f");
	ok &= Resolve(Uniform2f, loader, "glUniform2f");
	ok &= Resolve(Uniform3fv, loader, "glUniform3fv");
	ok &= Resolve(Uniform4fv, loader, "glUniform4fv");
	ok &= Resolve(UniformMatrix4fv, loader, "glUniformMatrix4fv");

	ok &= Resolve(DrawArrays, loader, "glDrawArrays");
	ok &= Resolve(DrawElements, loader, "glDrawElements");
	ok &= Resolve(DrawElementsBaseVertex, loader, "glDrawElementsBaseVertex");

	return ok;
}

} // namespace gl
