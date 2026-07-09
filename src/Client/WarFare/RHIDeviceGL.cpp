#include "StdAfx.h"
#include "RHIDeviceGL.h"
#include "GLLoader.h"

#include <SDL3/SDL.h>

#include <spdlog/spdlog.h>

void RHIDeviceGL::SetGLWindowAttributes()
{
	// A 3.3 core context is the floor for the fixed-function über-shader that
	// T6.7 needs; macOS only exposes core (3.2+) with forward-compat set.
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
}

RHIDeviceGL::RHIDeviceGL(SDL_Window* pWindow, bool bVSync) : m_pWindow(pWindow)
{
	m_pGLContext = SDL_GL_CreateContext(pWindow);
	if (m_pGLContext == nullptr)
	{
		spdlog::error("RHIDeviceGL: SDL_GL_CreateContext failed: {}", SDL_GetError());
		return;
	}

	if (!SDL_GL_MakeCurrent(pWindow, static_cast<SDL_GLContext>(m_pGLContext)))
	{
		spdlog::error("RHIDeviceGL: SDL_GL_MakeCurrent failed: {}", SDL_GetError());
		return;
	}

	if (!gl::Load(SDL_GL_GetProcAddress))
	{
		spdlog::error("RHIDeviceGL: failed to resolve required OpenGL entry points");
		return;
	}

	// vsync (best-effort; adaptive/late-swap not required for bring-up).
	SDL_GL_SetSwapInterval(bVSync ? 1 : 0);

	int pixelW = 0, pixelH = 0;
	SDL_GetWindowSizeInPixels(pWindow, &pixelW, &pixelH);
	gl::Viewport(0, 0, pixelW, pixelH);
	gl::Enable(gl::DEPTH_TEST);

	const gl::Ubyte* pRenderer = gl::GetString(gl::RENDERER);
	const gl::Ubyte* pVersion  = gl::GetString(gl::VERSION);
	spdlog::info("RHIDeviceGL: OpenGL {} on {}",
		pVersion ? reinterpret_cast<const char*>(pVersion) : "?",
		pRenderer ? reinterpret_cast<const char*>(pRenderer) : "?");

	m_bValid = true;
}

RHIDeviceGL::~RHIDeviceGL()
{
	if (m_pGLContext != nullptr)
	{
		SDL_GL_DestroyContext(static_cast<SDL_GLContext>(m_pGLContext));
		m_pGLContext = nullptr;
	}
}

HRESULT RHIDeviceGL::Clear(DWORD flags, D3DCOLOR color, float z, DWORD /*stencil*/)
{
	gl::Bitfield mask = 0;
	if (flags & D3DCLEAR_TARGET)
		mask |= gl::COLOR_BUFFER_BIT;
	if (flags & D3DCLEAR_ZBUFFER)
		mask |= gl::DEPTH_BUFFER_BIT;
	if (flags & D3DCLEAR_STENCIL)
		mask |= gl::STENCIL_BUFFER_BIT;

	// D3DCOLOR is 0xAARRGGBB.
	const gl::Clampf a = ((color >> 24) & 0xFF) / 255.0f;
	const gl::Clampf r = ((color >> 16) & 0xFF) / 255.0f;
	const gl::Clampf g = ((color >> 8) & 0xFF) / 255.0f;
	const gl::Clampf b = (color & 0xFF) / 255.0f;

	gl::ClearColor(r, g, b, a);
	gl::ClearDepth(z);
	gl::Clear(mask);
	return D3D_OK;
}

HRESULT RHIDeviceGL::Present()
{
	SDL_GL_SwapWindow(m_pWindow);
	return RHIDeviceNull::Present(); // keep the present counter for diagnostics
}
