#include "StdAfx.h"
#include "GLLoader.h"

namespace gl
{
void (*ClearColor)(Clampf, Clampf, Clampf, Clampf) = nullptr;
void (*ClearDepth)(Clampd)                         = nullptr;
void (*Clear)(Bitfield)                            = nullptr;
void (*Viewport)(Int, Int, Sizei, Sizei)           = nullptr;
void (*Scissor)(Int, Int, Sizei, Sizei)            = nullptr;
void (*Enable)(Enum)                               = nullptr;
void (*Disable)(Enum)                              = nullptr;
Enum (*GetError)()                                 = nullptr;
const Ubyte* (*GetString)(Enum)                    = nullptr;

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
	ok &= Resolve(Scissor, loader, "glScissor");
	ok &= Resolve(Enable, loader, "glEnable");
	ok &= Resolve(Disable, loader, "glDisable");
	ok &= Resolve(GetError, loader, "glGetError");
	ok &= Resolve(GetString, loader, "glGetString");
	return ok;
}

} // namespace gl
