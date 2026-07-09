#ifndef CLIENT_WARFARE_RHIDEVICEGL_H
#define CLIENT_WARFARE_RHIDEVICEGL_H

#pragma once

// OpenGL RHI backend, bring-up stage (docs/PORT_POSIX_PLAN.md, T6.5).
//
// It derives from RHIDeviceNull so it inherits all the fixed-function state
// bookkeeping and no-op stubs, and only overrides the frame boundary to talk
// to a real GL context: clear the window to the requested colour and present
// via SDL_GL_SwapWindow. Geometry, textures and the fixed-function
// über-shader arrive in T6.6/T6.7, overriding more of the base as they land.

#include <N3Base/RHI/RHIDeviceNull.h>

struct SDL_Window;

class RHIDeviceGL : public RHIDeviceNull
{
public:
	// Must be called before SDL_CreateWindow so the window is GL-capable.
	static void SetGLWindowAttributes();

	RHIDeviceGL(SDL_Window* pWindow, bool bVSync);
	~RHIDeviceGL() override;

	RHIDeviceGL(const RHIDeviceGL&)            = delete;
	RHIDeviceGL& operator=(const RHIDeviceGL&) = delete;

	// True once the context is created and every GL entry point resolved.
	bool IsValid() const
	{
		return m_bValid;
	}

	HRESULT Clear(DWORD flags, D3DCOLOR color, float z, DWORD stencil) override;
	HRESULT Present() override;

private:
	SDL_Window* m_pWindow  = nullptr;
	void* m_pGLContext     = nullptr; // SDL_GLContext (opaque)
	bool m_bValid          = false;
};

#endif // CLIENT_WARFARE_RHIDEVICEGL_H
