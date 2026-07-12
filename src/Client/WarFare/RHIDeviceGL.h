#ifndef CLIENT_WARFARE_RHIDEVICEGL_H
#define CLIENT_WARFARE_RHIDEVICEGL_H

#pragma once

// OpenGL RHI backend (docs/PORT_POSIX_PLAN.md, T6.5-T6.7).
//
// Derives from RHIDeviceNull so all the fixed-function state bookkeeping
// (render states, stage states, sampler states, transforms, lights, material,
// viewport) is inherited; this class turns that recorded state into GL state
// and a fixed-function "uber-shader" at draw time:
//
//   T6.5 - context, clear, present, vsync.
//   T6.6 - VBO/IBO mirrors of the RHI buffers, a streaming buffer for the
//          Draw*UP calls, DXT/BGRA texture uploads, BGRA vertex colors.
//   T6.7 - GLSL 330 uber-shader: WVP matrices with D3D->GL depth remap,
//          3 texture stages (DISABLE/SELECTARG/MODULATE(2X/4X)/ADD),
//          per-vertex lighting (directional+point), linear fog, alpha test,
//          and the pre-transformed (XYZRHW) screen-space path.
//
// GL resources (textures/buffers) extend the Null ones: the system-memory
// copy stays the source of truth (Lock/Unlock keep working identically), and
// the GL object is (re)uploaded lazily at bind time after each Unlock.

#include "GLTranslate.h"

#include <N3Base/RHI/RHIDeviceNull.h>

struct SDL_Window;

/// GL texture: RHITextureNull storage + a lazily created/uploaded GL object.
class RHITextureGL : public RHITextureNull
{
public:
	using RHITextureNull::RHITextureNull;

	HRESULT UnlockRect(UINT level) override;
	ULONG Release() override;

	/// Returns the GL texture name, creating/uploading it if needed.
	/// Requires the GL context to be current.
	gl::Uint GLTexture();

protected:
	gl::Uint m_uTexture = 0;
	bool m_bDirtyGL     = true;
};

/// GL vertex buffer: system-memory storage mirrored into a VBO on demand.
class RHIVertexBufferGL : public RHIVertexBufferNull
{
public:
	using RHIVertexBufferNull::RHIVertexBufferNull;

	HRESULT Unlock() override;
	ULONG Release() override;

	/// Returns the VBO name, creating/uploading it if needed (binds ARRAY_BUFFER).
	gl::Uint GLBuffer();

protected:
	gl::Uint m_uBuffer = 0;
	bool m_bDirtyGL    = true;
};

class RHIIndexBufferGL : public RHIIndexBufferNull
{
public:
	using RHIIndexBufferNull::RHIIndexBufferNull;

	HRESULT Unlock() override;
	ULONG Release() override;

	/// Returns the IBO name, creating/uploading it if needed
	/// (binds ELEMENT_ARRAY_BUFFER).
	gl::Uint GLBuffer();

protected:
	gl::Uint m_uBuffer = 0;
	bool m_bDirtyGL    = true;
};

class RHIDeviceGL : public RHIDeviceNull
{
public:
	// Must be called before SDL_CreateWindow so the window is GL-capable.
	static void SetGLWindowAttributes();

	RHIDeviceGL(SDL_Window* pWindow, bool bVSync);
	~RHIDeviceGL() override;

	RHIDeviceGL(const RHIDeviceGL&)            = delete;
	RHIDeviceGL& operator=(const RHIDeviceGL&) = delete;

	// True once the context is created, every GL entry point resolved and the
	// uber-shader linked.
	bool IsValid() const
	{
		return m_bValid;
	}

	bool SupportsS3TC() const
	{
		return m_bS3TC;
	}

	/// The SDL_GLContext this backend created and made current. Lets a tool
	/// share one context between the engine renderer and its ImGui overlay
	/// (docs/ASSET_EXPLORER_PLAN.md, M1).
	void* GLContext() const
	{
		return m_pGLContext;
	}

	/// Reads the RGBA of the framebuffer's center pixel (diagnostics/smoke).
	bool ReadCenterPixel(uint8_t rgbaOut[4]);

	/// Reads one pixel back from an offscreen render target (diagnostics/smoke;
	/// also the basis for thumbnail readback later). Binds the target's FBO,
	/// reads, and restores the previous binding. (x, y) is bottom-left origin.
	bool ReadRenderTargetPixel(IRHIRenderTarget* pTarget, int x, int y, uint8_t rgbaOut[4]);

	/// Dumps the current back buffer to a binary PPM (diagnostics: lets CI and
	/// the Mac bring-up inspect real frames without window-system capture).
	bool DumpFramePPM(const char* szPath);

	// --- Frame ---------------------------------------------------------------
	HRESULT Clear(DWORD flags, D3DCOLOR color, float z, DWORD stencil) override;
	HRESULT Present() override;

	// --- Offscreen render targets (M1) -----------------------------------------
	IRHIRenderTarget* CreateRenderTarget(const RHIRenderTargetDesc& desc) override;
	void BeginRenderTarget(IRHIRenderTarget* pTarget) override;
	void EndRenderTarget() override;

	// --- Resources -------------------------------------------------------------
	HRESULT CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format,
		D3DPOOL pool, IRHITexture** ppTexture) override;
	HRESULT CreateVertexBuffer(
		UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, IRHIVertexBuffer** ppBuffer) override;
	HRESULT CreateIndexBuffer(
		UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, IRHIIndexBuffer** ppBuffer) override;

	HRESULT SetTexture(DWORD stage, IRHITexture* pTexture) override;
	HRESULT SetStreamSource(
		UINT streamNumber, IRHIVertexBuffer* pBuffer, UINT offsetInBytes, UINT stride) override;

	// --- Draws -----------------------------------------------------------------
	HRESULT DrawPrimitive(
		D3DPRIMITIVETYPE primitiveType, UINT startVertex, UINT primitiveCount) override;
	HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE primitiveType, INT baseVertexIndex,
		UINT minVertexIndex, UINT numVertices, UINT startIndex, UINT primitiveCount) override;
	HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE primitiveType, UINT primitiveCount,
		const void* pVertexData, UINT vertexStride) override;
	HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE primitiveType, UINT minVertexIndex,
		UINT numVertices, UINT primitiveCount, const void* pIndexData, D3DFORMAT indexFormat,
		const void* pVertexData, UINT vertexStride) override;

private:
	static constexpr DWORD MAX_GL_STAGES = 3; // terrain uses stages 0..2
	static constexpr int MAX_GL_LIGHTS   = 8;

	bool BuildProgram();
	void SeedD3DDefaults();

	// Turns the recorded D3D state into GL state/uniforms for the next draw.
	bool BeginDraw(const gltr::FVFLayout& layout);
	void ApplyFixedState();
	void ApplyViewport(bool bPreTransformed);
	void ApplyUniforms(const gltr::FVFLayout& layout);
	void ApplyTexturesAndSamplers();
	void SetupVertexAttribs(const gltr::FVFLayout& layout, intptr_t baseOffset);

	SDL_Window* m_pWindow = nullptr;
	void* m_pGLContext    = nullptr; // SDL_GLContext (opaque)
	bool m_bValid         = false;
	bool m_bS3TC          = false;
	int m_iWinPixelW      = 0;
	int m_iWinPixelH      = 0;

	gl::Uint m_uVAO      = 0;
	gl::Uint m_uProgram  = 0;
	gl::Uint m_uStreamVB = 0; // Draw*UP staging
	gl::Uint m_uStreamIB = 0;
	gl::Uint m_auSamplers[MAX_GL_STAGES] = {};

	IRHITexture* m_apTextures[MAX_GL_STAGES] = {};
	UINT m_nStreamOffset = 0;
	UINT m_nStreamStride = 0;

	// Offscreen render-target state saved across BeginRenderTarget/EndRenderTarget.
	// ApplyViewport keys off m_iWinPixelW/H, so a bound target temporarily
	// overrides them with the target's size and restores on End.
	gl::Uint m_uSavedFBO = 0;
	int m_iSavedWinPixelW = 0;
	int m_iSavedWinPixelH = 0;
	bool m_bRenderTargetBound = false;

	// Uniform locations, cached at link time.
	struct Locations
	{
		gl::Int wvp            = -1;
		gl::Int world          = -1;
		gl::Int wv             = -1;
		gl::Int preTransformed = -1;
		gl::Int viewportSize   = -1;
		gl::Int hasNormal      = -1;
		gl::Int hasColor       = -1;
		gl::Int lighting       = -1;
		gl::Int globalAmbient  = -1;
		gl::Int matDiffuse     = -1;
		gl::Int matAmbient     = -1;
		gl::Int matEmissive    = -1;
		gl::Int lightCount     = -1;
		gl::Int lightPos       = -1; // vec4[8]: xyz + range
		gl::Int lightDir       = -1; // vec4[8]: xyz + D3DLIGHTTYPE
		gl::Int lightDiffuse   = -1;
		gl::Int lightAmbient   = -1;
		gl::Int lightAtt       = -1; // vec4[8]: att0 att1 att2 -
		gl::Int texBound       = -1;
		gl::Int colorOp        = -1;
		gl::Int colorArg1      = -1;
		gl::Int colorArg2      = -1;
		gl::Int alphaOp        = -1;
		gl::Int alphaArg1      = -1;
		gl::Int alphaArg2      = -1;
		gl::Int tfactor        = -1;
		gl::Int alphaTestFunc  = -1;
		gl::Int alphaRef       = -1;
		gl::Int fogEnable      = -1;
		gl::Int fogColor       = -1;
		gl::Int fogStart       = -1;
		gl::Int fogEnd         = -1;
	} m_Locs;
};

#endif // CLIENT_WARFARE_RHIDEVICEGL_H
