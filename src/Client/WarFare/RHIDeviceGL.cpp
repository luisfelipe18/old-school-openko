#include "StdAfx.h"
#include "RHIDeviceGL.h"
#include "GLLoader.h"

#include <N3Base/N3Base.h>

#include <SDL3/SDL.h>

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ============================================================================
// Fixed-function uber-shader (T6.7)
//
// One program covers every path the engine's D3D9 fixed-function usage needs:
// world/view/projection with the D3D [0,1] -> GL [-1,1] depth remap, the
// pre-transformed XYZRHW screen-space path, per-vertex lighting
// (directional + point), 3 texture stages with the D3DTOP subset the engine
// uses, TFACTOR, alpha test, and linear fog. Stage ops/args arrive as D3D
// token values so the mapping stays greppable against d3d9types.h.
// ============================================================================

namespace
{
const char* const VERTEX_SHADER_SOURCE = R"GLSL(#version 330 core
layout(location = 0) in vec4 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aUV0;
layout(location = 4) in vec2 aUV1;

uniform mat4 uWVP;
uniform mat4 uWorld;
uniform mat4 uWV;
uniform int  uPreTransformed;
uniform vec2 uViewportSize;
uniform int  uHasNormal;
uniform int  uHasColor;
uniform int  uLighting;
uniform vec4 uGlobalAmbient;
uniform vec4 uMatDiffuse;
uniform vec4 uMatAmbient;
uniform vec4 uMatEmissive;
uniform int  uLightCount;
uniform vec4 uLightPos[8];     // xyz + range
uniform vec4 uLightDir[8];     // xyz + D3DLIGHTTYPE (1 = point, 3 = directional)
uniform vec4 uLightDiffuse[8];
uniform vec4 uLightAmbient[8];
uniform vec4 uLightAtt[8];     // attenuation0/1/2

out vec4  vColor;
out vec2  vUV0;
out vec2  vUV1;
out float vFogDepth;

void main()
{
	if (uPreTransformed != 0)
	{
		// XYZRHW: x/y in render-target pixels, z already in [0,1].
		float x     = aPos.x * 2.0 / uViewportSize.x - 1.0;
		float y     = 1.0 - aPos.y * 2.0 / uViewportSize.y;
		gl_Position = vec4(x, y, aPos.z * 2.0 - 1.0, 1.0);
		vFogDepth   = 0.0;
	}
	else
	{
		vec4 clip   = uWVP * vec4(aPos.xyz, 1.0);
		clip.z      = 2.0 * clip.z - clip.w; // D3D depth [0,1] -> GL [-1,1]
		gl_Position = clip;
		vFogDepth   = (uWV * vec4(aPos.xyz, 1.0)).z;
	}

	vec4 base = (uHasColor != 0) ? aColor : vec4(1.0);

	// D3D9 per-vertex lighting; the engine feeds world-space lights.
	if (uLighting != 0 && uHasNormal != 0 && uPreTransformed == 0)
	{
		vec3 n    = normalize(mat3(uWorld) * aNormal);
		vec3 wpos = (uWorld * vec4(aPos.xyz, 1.0)).xyz;
		vec3 diff = vec3(0.0);
		vec3 amb  = uGlobalAmbient.rgb;
		for (int i = 0; i < uLightCount; ++i)
		{
			vec3 l;
			float atten = 1.0;
			if (uLightDir[i].w > 2.5) // directional
			{
				l = -normalize(uLightDir[i].xyz);
			}
			else // point
			{
				vec3 d     = uLightPos[i].xyz - wpos;
				float dist = length(d);
				if (dist > uLightPos[i].w)
					continue;
				l     = d / max(dist, 1e-5);
				atten = 1.0
						/ max(uLightAtt[i].x + uLightAtt[i].y * dist
								  + uLightAtt[i].z * dist * dist,
							1e-5);
			}
			diff += uLightDiffuse[i].rgb * max(dot(n, l), 0.0) * atten;
			amb  += uLightAmbient[i].rgb * atten;
		}
		vec3 lit = uMatEmissive.rgb + uMatAmbient.rgb * amb + uMatDiffuse.rgb * diff;
		base     = vec4(clamp(lit, 0.0, 1.0), uMatDiffuse.a);
	}

	vColor = base;
	vUV0   = aUV0;
	vUV1   = aUV1;
}
)GLSL";

const char* const FRAGMENT_SHADER_SOURCE = R"GLSL(#version 330 core
in vec4  vColor;
in vec2  vUV0;
in vec2  vUV1;
in float vFogDepth;

uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform sampler2D uTex2;
uniform int  uTexBound[3];
uniform int  uColorOp[3];   // D3DTOP_* tokens
uniform int  uColorArg1[3]; // D3DTA_* tokens
uniform int  uColorArg2[3];
uniform int  uAlphaOp[3];
uniform int  uAlphaArg1[3];
uniform int  uAlphaArg2[3];
uniform vec4 uTFactor;
uniform int  uAlphaTestFunc; // 0 = off, else D3DCMP_*
uniform float uAlphaRef;
uniform int  uFogEnable;
uniform vec4 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;

out vec4 oColor;

vec4 SelectArg(int arg, vec4 current, vec4 tex)
{
	if (arg == 0) return vColor;   // D3DTA_DIFFUSE
	if (arg == 1) return current;  // D3DTA_CURRENT
	if (arg == 2) return tex;      // D3DTA_TEXTURE
	if (arg == 3) return uTFactor; // D3DTA_TFACTOR
	return vec4(1.0);
}

vec3 CombineRGB(int op, vec3 a1, vec3 a2)
{
	if (op == 2) return a1;                              // SELECTARG1
	if (op == 3) return a2;                              // SELECTARG2
	if (op == 4) return a1 * a2;                         // MODULATE
	if (op == 5) return clamp(a1 * a2 * 2.0, 0.0, 1.0);  // MODULATE2X
	if (op == 6) return clamp(a1 * a2 * 4.0, 0.0, 1.0);  // MODULATE4X
	if (op == 7) return clamp(a1 + a2, 0.0, 1.0);        // ADD
	return a1 * a2;                                      // unknown -> MODULATE
}

float CombineA(int op, float a1, float a2)
{
	if (op == 2) return a1;
	if (op == 3) return a2;
	if (op == 4) return a1 * a2;
	if (op == 5) return clamp(a1 * a2 * 2.0, 0.0, 1.0);
	if (op == 6) return clamp(a1 * a2 * 4.0, 0.0, 1.0);
	if (op == 7) return clamp(a1 + a2, 0.0, 1.0);
	return a1 * a2;
}

vec4 RunStage(int i, vec4 current, vec4 tex)
{
	vec4 c1      = SelectArg(uColorArg1[i], current, tex);
	vec4 c2      = SelectArg(uColorArg2[i], current, tex);
	vec4 outCol  = current;
	outCol.rgb   = CombineRGB(uColorOp[i], c1.rgb, c2.rgb);
	if (uAlphaOp[i] != 1) // D3DTOP_DISABLE keeps the current alpha
	{
		vec4 a1  = SelectArg(uAlphaArg1[i], current, tex);
		vec4 a2  = SelectArg(uAlphaArg2[i], current, tex);
		outCol.a = CombineA(uAlphaOp[i], a1.a, a2.a);
	}
	return outCol;
}

bool AlphaTestPasses(float a)
{
	if (uAlphaTestFunc == 1) return false;              // NEVER
	if (uAlphaTestFunc == 2) return a <  uAlphaRef;     // LESS
	if (uAlphaTestFunc == 3) return a == uAlphaRef;     // EQUAL
	if (uAlphaTestFunc == 4) return a <= uAlphaRef;     // LESSEQUAL
	if (uAlphaTestFunc == 5) return a >  uAlphaRef;     // GREATER
	if (uAlphaTestFunc == 6) return a != uAlphaRef;     // NOTEQUAL
	if (uAlphaTestFunc == 7) return a >= uAlphaRef;     // GREATEREQUAL
	return true;                                        // ALWAYS
}

void main()
{
	// Unbound texture stages sample as opaque white so MODULATE passes
	// diffuse through (matches how the engine configures unused stages).
	vec4 tex0 = (uTexBound[0] != 0) ? texture(uTex0, vUV0) : vec4(1.0);
	vec4 tex1 = (uTexBound[1] != 0) ? texture(uTex1, vUV1) : vec4(1.0);
	vec4 tex2 = (uTexBound[2] != 0) ? texture(uTex2, vUV1) : vec4(1.0);

	vec4 current = vColor;
	bool stop    = (uColorOp[0] == 1); // D3DTOP_DISABLE ends the chain
	if (!stop)
	{
		current = RunStage(0, current, tex0);
		stop    = (uColorOp[1] == 1);
	}
	if (!stop)
	{
		current = RunStage(1, current, tex1);
		stop    = (uColorOp[2] == 1);
	}
	if (!stop)
	{
		current = RunStage(2, current, tex2);
	}

	if (uAlphaTestFunc != 0 && !AlphaTestPasses(current.a))
		discard;

	if (uFogEnable != 0)
	{
		float f     = clamp((uFogEnd - vFogDepth) / max(uFogEnd - uFogStart, 1e-5), 0.0, 1.0);
		current.rgb = mix(uFogColor.rgb, current.rgb, f);
	}

	oColor = current;
}
)GLSL";

// D3DCOLOR (0xAARRGGBB) -> float RGBA.
void ColorToFloats(D3DCOLOR color, float out[4])
{
	out[0] = ((color >> 16) & 0xFF) / 255.0f;
	out[1] = ((color >> 8) & 0xFF) / 255.0f;
	out[2] = (color & 0xFF) / 255.0f;
	out[3] = ((color >> 24) & 0xFF) / 255.0f;
}

// Render states like FOGSTART store IEEE float bits in the DWORD.
float FloatBits(DWORD dwValue)
{
	float f = 0.0f;
	std::memcpy(&f, &dwValue, sizeof(f));
	return f;
}

gl::Uint CompileShaderStage(gl::Enum type, const char* source)
{
	const gl::Uint shader = gl::CreateShader(type);
	gl::ShaderSource(shader, 1, &source, nullptr);
	gl::CompileShader(shader);

	gl::Int status = 0;
	gl::GetShaderiv(shader, gl::COMPILE_STATUS, &status);
	if (status == 0)
	{
		char log[2048] = {};
		gl::GetShaderInfoLog(shader, sizeof(log), nullptr, log);
		spdlog::error("RHIDeviceGL: shader compile failed:\n{}", log);
		gl::DeleteShader(shader);
		return 0;
	}
	return shader;
}
} // namespace

// ============================================================================
// GL resources (T6.6)
// ============================================================================

HRESULT RHITextureGL::UnlockRect(UINT level)
{
	m_bDirtyGL = true;
	return RHITextureNull::UnlockRect(level);
}

ULONG RHITextureGL::Release()
{
	if (m_uTexture != 0)
		gl::DeleteTextures(1, &m_uTexture);
	return RHITextureNull::Release(); // deletes this
}

gl::Uint RHITextureGL::GLTexture()
{
	if (m_uTexture == 0)
	{
		gl::GenTextures(1, &m_uTexture);
		m_bDirtyGL = true;
	}

	gl::BindTexture(gl::TEXTURE_2D, m_uTexture);
	if (!m_bDirtyGL)
		return m_uTexture;

	const gltr::TexUploadFormat fmt = gltr::TranslateTexFormat(m_eFormat);
	if (!fmt.valid)
	{
		spdlog::warn("RHIDeviceGL: unsupported texture format {}", uint32_t(m_eFormat));
		m_bDirtyGL = false;
		return m_uTexture;
	}

	for (size_t i = 0; i < m_Levels.size(); ++i)
	{
		const Level& level = m_Levels[i];
		if (fmt.compressed)
		{
			gl::CompressedTexImage2D(gl::TEXTURE_2D, static_cast<gl::Int>(i), fmt.internalFormat,
				static_cast<gl::Sizei>(level.width), static_cast<gl::Sizei>(level.height), 0,
				static_cast<gl::Sizei>(level.storage.size()), level.storage.data());
		}
		else
		{
			gl::TexImage2D(gl::TEXTURE_2D, static_cast<gl::Int>(i),
				static_cast<gl::Int>(fmt.internalFormat), static_cast<gl::Sizei>(level.width),
				static_cast<gl::Sizei>(level.height), 0, fmt.format, fmt.type,
				level.storage.data());
		}
	}

	// The engine's mip chains stop at 4x4, not 1x1: cap the level count so the
	// texture stays mipmap-complete for GL.
	gl::TexParameteri(
		gl::TEXTURE_2D, gl::TEXTURE_MAX_LEVEL, static_cast<gl::Int>(m_Levels.size()) - 1);

	m_bDirtyGL = false;
	return m_uTexture;
}

HRESULT RHIVertexBufferGL::Unlock()
{
	m_bDirtyGL = true;
	return RHIVertexBufferNull::Unlock();
}

ULONG RHIVertexBufferGL::Release()
{
	if (m_uBuffer != 0)
		gl::DeleteBuffers(1, &m_uBuffer);
	return RHIVertexBufferNull::Release(); // deletes this
}

gl::Uint RHIVertexBufferGL::GLBuffer()
{
	if (m_uBuffer == 0)
	{
		gl::GenBuffers(1, &m_uBuffer);
		m_bDirtyGL = true;
	}

	gl::BindBuffer(gl::ARRAY_BUFFER, m_uBuffer);
	if (m_bDirtyGL)
	{
		gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::Sizeiptr>(m_Storage.size()),
			m_Storage.data(), gl::STATIC_DRAW);
		m_bDirtyGL = false;
	}
	return m_uBuffer;
}

HRESULT RHIIndexBufferGL::Unlock()
{
	m_bDirtyGL = true;
	return RHIIndexBufferNull::Unlock();
}

ULONG RHIIndexBufferGL::Release()
{
	if (m_uBuffer != 0)
		gl::DeleteBuffers(1, &m_uBuffer);
	return RHIIndexBufferNull::Release(); // deletes this
}

gl::Uint RHIIndexBufferGL::GLBuffer()
{
	if (m_uBuffer == 0)
	{
		gl::GenBuffers(1, &m_uBuffer);
		m_bDirtyGL = true;
	}

	gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, m_uBuffer);
	if (m_bDirtyGL)
	{
		gl::BufferData(gl::ELEMENT_ARRAY_BUFFER, static_cast<gl::Sizeiptr>(m_Storage.size()),
			m_Storage.data(), gl::STATIC_DRAW);
		m_bDirtyGL = false;
	}
	return m_uBuffer;
}

// ============================================================================
// Device setup
// ============================================================================

void RHIDeviceGL::SetGLWindowAttributes()
{
	// A 3.3 core context is the floor for the fixed-function uber-shader;
	// macOS only exposes core (3.2+) with forward-compat set.
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

	SDL_GL_SetSwapInterval(bVSync ? 1 : 0);

	SDL_GetWindowSizeInPixels(pWindow, &m_iWinPixelW, &m_iWinPixelH);
	gl::Viewport(0, 0, m_iWinPixelW, m_iWinPixelH);
	gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);

	const gl::Ubyte* pRenderer = gl::GetString(gl::RENDERER);
	const gl::Ubyte* pVersion  = gl::GetString(gl::VERSION);
	spdlog::info("RHIDeviceGL: OpenGL {} on {}",
		pVersion ? reinterpret_cast<const char*>(pVersion) : "?",
		pRenderer ? reinterpret_cast<const char*>(pRenderer) : "?");

	// S3TC (DXT) support gates N3Texture's compressed path via s_dwTextureCaps,
	// exactly like CN3Eng does on Windows from the D3D caps.
	gl::Int numExtensions = 0;
	gl::GetIntegerv(gl::NUM_EXTENSIONS, &numExtensions);
	for (gl::Int i = 0; i < numExtensions; ++i)
	{
		const char* pExt = reinterpret_cast<const char*>(
			gl::GetStringi(gl::EXTENSIONS, static_cast<gl::Uint>(i)));
		if (pExt != nullptr && std::strcmp(pExt, "GL_EXT_texture_compression_s3tc") == 0)
		{
			m_bS3TC = true;
			break;
		}
	}
	if (m_bS3TC)
	{
		CN3Base::s_dwTextureCaps |= TEX_CAPS_DXT1 | TEX_CAPS_DXT2 | TEX_CAPS_DXT3 | TEX_CAPS_DXT4
									| TEX_CAPS_DXT5;
	}

	gl::Int maxTexSize = 256;
	gl::GetIntegerv(gl::MAX_TEXTURE_SIZE, &maxTexSize);
	CN3Base::s_DevCaps.MaxTextureWidth  = static_cast<DWORD>(maxTexSize);
	CN3Base::s_DevCaps.MaxTextureHeight = static_cast<DWORD>(maxTexSize);

	// Core profile needs a VAO bound for vertex attribs; one is enough since
	// attrib pointers are respecified per draw.
	gl::GenVertexArrays(1, &m_uVAO);
	gl::BindVertexArray(m_uVAO);
	gl::GenBuffers(1, &m_uStreamVB);
	gl::GenBuffers(1, &m_uStreamIB);
	gl::GenSamplers(MAX_GL_STAGES, m_auSamplers);

	if (!BuildProgram())
		return;

	SeedD3DDefaults();

	spdlog::info("RHIDeviceGL: S3TC {}, max texture size {}",
		m_bS3TC ? "supported" : "NOT supported", maxTexSize);
	m_bValid = true;
}

RHIDeviceGL::~RHIDeviceGL()
{
	if (m_pGLContext != nullptr)
	{
		if (m_uProgram != 0)
			gl::DeleteProgram(m_uProgram);
		if (m_uVAO != 0)
			gl::DeleteVertexArrays(1, &m_uVAO);
		if (m_uStreamVB != 0)
			gl::DeleteBuffers(1, &m_uStreamVB);
		if (m_uStreamIB != 0)
			gl::DeleteBuffers(1, &m_uStreamIB);
		if (m_auSamplers[0] != 0)
			gl::DeleteSamplers(MAX_GL_STAGES, m_auSamplers);

		SDL_GL_DestroyContext(static_cast<SDL_GLContext>(m_pGLContext));
		m_pGLContext = nullptr;
	}
}

bool RHIDeviceGL::BuildProgram()
{
	const gl::Uint vs = CompileShaderStage(gl::VERTEX_SHADER, VERTEX_SHADER_SOURCE);
	if (vs == 0)
		return false;
	const gl::Uint fs = CompileShaderStage(gl::FRAGMENT_SHADER, FRAGMENT_SHADER_SOURCE);
	if (fs == 0)
	{
		gl::DeleteShader(vs);
		return false;
	}

	m_uProgram = gl::CreateProgram();
	gl::AttachShader(m_uProgram, vs);
	gl::AttachShader(m_uProgram, fs);
	gl::LinkProgram(m_uProgram);
	gl::DeleteShader(vs);
	gl::DeleteShader(fs);

	gl::Int status = 0;
	gl::GetProgramiv(m_uProgram, gl::LINK_STATUS, &status);
	if (status == 0)
	{
		char log[2048] = {};
		gl::GetProgramInfoLog(m_uProgram, sizeof(log), nullptr, log);
		spdlog::error("RHIDeviceGL: program link failed:\n{}", log);
		return false;
	}

	const auto Loc = [this](const char* name) { return gl::GetUniformLocation(m_uProgram, name); };
	m_Locs.wvp            = Loc("uWVP");
	m_Locs.world          = Loc("uWorld");
	m_Locs.wv             = Loc("uWV");
	m_Locs.preTransformed = Loc("uPreTransformed");
	m_Locs.viewportSize   = Loc("uViewportSize");
	m_Locs.hasNormal      = Loc("uHasNormal");
	m_Locs.hasColor       = Loc("uHasColor");
	m_Locs.lighting       = Loc("uLighting");
	m_Locs.globalAmbient  = Loc("uGlobalAmbient");
	m_Locs.matDiffuse     = Loc("uMatDiffuse");
	m_Locs.matAmbient     = Loc("uMatAmbient");
	m_Locs.matEmissive    = Loc("uMatEmissive");
	m_Locs.lightCount     = Loc("uLightCount");
	m_Locs.lightPos       = Loc("uLightPos");
	m_Locs.lightDir       = Loc("uLightDir");
	m_Locs.lightDiffuse   = Loc("uLightDiffuse");
	m_Locs.lightAmbient   = Loc("uLightAmbient");
	m_Locs.lightAtt       = Loc("uLightAtt");
	m_Locs.texBound       = Loc("uTexBound");
	m_Locs.colorOp        = Loc("uColorOp");
	m_Locs.colorArg1      = Loc("uColorArg1");
	m_Locs.colorArg2      = Loc("uColorArg2");
	m_Locs.alphaOp        = Loc("uAlphaOp");
	m_Locs.alphaArg1      = Loc("uAlphaArg1");
	m_Locs.alphaArg2      = Loc("uAlphaArg2");
	m_Locs.tfactor        = Loc("uTFactor");
	m_Locs.alphaTestFunc  = Loc("uAlphaTestFunc");
	m_Locs.alphaRef       = Loc("uAlphaRef");
	m_Locs.fogEnable      = Loc("uFogEnable");
	m_Locs.fogColor       = Loc("uFogColor");
	m_Locs.fogStart       = Loc("uFogStart");
	m_Locs.fogEnd         = Loc("uFogEnd");

	// Bind the fixed sampler uniforms to texture units 0..2 once.
	gl::UseProgram(m_uProgram);
	gl::Uniform1i(Loc("uTex0"), 0);
	gl::Uniform1i(Loc("uTex1"), 1);
	gl::Uniform1i(Loc("uTex2"), 2);

	return true;
}

void RHIDeviceGL::SeedD3DDefaults()
{
	// D3D9 device defaults the engine implicitly relies on (the Null base
	// returns 0 for unset states, which is not what D3D would report).
	SetRenderState(D3DRS_ZENABLE, TRUE);
	SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	SetRenderState(D3DRS_ALPHAREF, 0);
	SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);
	SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
	SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);
	SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
	SetRenderState(D3DRS_LIGHTING, TRUE);
	SetRenderState(D3DRS_AMBIENT, 0);
	SetRenderState(D3DRS_FOGENABLE, FALSE);
	SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);

	for (DWORD stage = 0; stage < MAX_GL_STAGES; ++stage)
	{
		SetTextureStageState(stage, D3DTSS_COLOROP, stage == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE);
		SetTextureStageState(stage, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		SetTextureStageState(stage, D3DTSS_COLORARG2, D3DTA_CURRENT);
		SetTextureStageState(
			stage, D3DTSS_ALPHAOP, stage == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE);
		SetTextureStageState(stage, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		SetTextureStageState(stage, D3DTSS_ALPHAARG2, D3DTA_CURRENT);

		SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
		SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		SetSamplerState(stage, D3DSAMP_BORDERCOLOR, 0);
	}

	// Default viewport: the whole window (what D3D gives a fresh device).
	D3DVIEWPORT9 viewport = {};
	viewport.Width        = static_cast<DWORD>(m_iWinPixelW);
	viewport.Height       = static_cast<DWORD>(m_iWinPixelH);
	viewport.MinZ         = 0.0f;
	viewport.MaxZ         = 1.0f;
	SetViewport(&viewport);
}

// ============================================================================
// Frame
// ============================================================================

HRESULT RHIDeviceGL::Clear(DWORD flags, D3DCOLOR color, float z, DWORD /*stencil*/)
{
	gl::Bitfield mask = 0;
	if (flags & D3DCLEAR_TARGET)
		mask |= gl::COLOR_BUFFER_BIT;
	if (flags & D3DCLEAR_ZBUFFER)
		mask |= gl::DEPTH_BUFFER_BIT;
	if (flags & D3DCLEAR_STENCIL)
		mask |= gl::STENCIL_BUFFER_BIT;

	float rgba[4];
	ColorToFloats(color, rgba);

	// glClear honors scissor and the depth mask; D3D Clear does not. Every
	// draw reapplies the full state, so forcing them here is safe.
	gl::Disable(gl::SCISSOR_TEST);
	gl::DepthMask(1);
	gl::ClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
	gl::ClearDepth(z);
	gl::Clear(mask);
	return D3D_OK;
}

HRESULT RHIDeviceGL::Present()
{
	SDL_GL_SwapWindow(m_pWindow);

	// The window can change size at runtime (Alt+Enter windowed/fullscreen
	// toggle, window-manager resizes); the cached pixel size feeds the
	// Y-flip in SetViewport/SetScissorRect and the RHW mapping, so refresh
	// it once per frame rather than trusting the size captured at init.
	SDL_GetWindowSizeInPixels(m_pWindow, &m_iWinPixelW, &m_iWinPixelH);

	return RHIDeviceNull::Present(); // keep the present counter for diagnostics
}

namespace
{
// Concrete GL render target: an FBO with a color texture and (optionally) a
// depth renderbuffer. The color texture name doubles as the ImGui texture id.
class RHIRenderTargetGL : public IRHIRenderTarget
{
public:
	RHIRenderTargetGL(UINT width, UINT height, gl::Uint fbo, gl::Uint color, gl::Uint depthRb)
		: m_width(width), m_height(height), m_fbo(fbo), m_color(color), m_depthRb(depthRb)
	{
	}

	~RHIRenderTargetGL() override
	{
		if (m_color != 0)
			gl::DeleteTextures(1, &m_color);
		if (m_depthRb != 0)
			gl::DeleteRenderbuffers(1, &m_depthRb);
		if (m_fbo != 0)
			gl::DeleteFramebuffers(1, &m_fbo);
	}

	UINT Width() const override  { return m_width; }
	UINT Height() const override { return m_height; }

	void* ColorHandle() const override
	{
		return reinterpret_cast<void*>(static_cast<uintptr_t>(m_color));
	}

	gl::Uint FBO() const { return m_fbo; }

private:
	UINT m_width;
	UINT m_height;
	gl::Uint m_fbo;
	gl::Uint m_color;
	gl::Uint m_depthRb;
};
} // namespace

IRHIRenderTarget* RHIDeviceGL::CreateRenderTarget(const RHIRenderTargetDesc& desc)
{
	if (!m_bValid || desc.width == 0 || desc.height == 0)
		return nullptr;

	const auto w = static_cast<gl::Sizei>(desc.width);
	const auto h = static_cast<gl::Sizei>(desc.height);

	// Remember the binding to restore it - creating an FBO must not disturb the
	// caller's current target (window or another RT).
	gl::Int prevFBO = 0;
	gl::GetIntegerv(gl::FRAMEBUFFER_BINDING, &prevFBO);

	gl::Uint color = 0;
	gl::GenTextures(1, &color);
	gl::BindTexture(gl::TEXTURE_2D, color);
	gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
	gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
	gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
	gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
	gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
	gl::BindTexture(gl::TEXTURE_2D, 0);

	gl::Uint depthRb = 0;
	if (desc.depth)
	{
		gl::GenRenderbuffers(1, &depthRb);
		gl::BindRenderbuffer(gl::RENDERBUFFER, depthRb);
		gl::RenderbufferStorage(gl::RENDERBUFFER, gl::DEPTH_COMPONENT24, w, h);
		gl::BindRenderbuffer(gl::RENDERBUFFER, 0);
	}

	gl::Uint fbo = 0;
	gl::GenFramebuffers(1, &fbo);
	gl::BindFramebuffer(gl::FRAMEBUFFER, fbo);
	gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, color, 0);
	if (depthRb != 0)
		gl::FramebufferRenderbuffer(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::RENDERBUFFER, depthRb);

	const gl::Enum status = gl::CheckFramebufferStatus(gl::FRAMEBUFFER);
	gl::BindFramebuffer(gl::FRAMEBUFFER, static_cast<gl::Uint>(prevFBO));

	if (status != gl::FRAMEBUFFER_COMPLETE)
	{
		spdlog::error("RHIDeviceGL: render target incomplete (status 0x{:X})", status);
		if (color != 0)
			gl::DeleteTextures(1, &color);
		if (depthRb != 0)
			gl::DeleteRenderbuffers(1, &depthRb);
		if (fbo != 0)
			gl::DeleteFramebuffers(1, &fbo);
		return nullptr;
	}

	return new RHIRenderTargetGL(desc.width, desc.height, fbo, color, depthRb);
}

void RHIDeviceGL::BeginRenderTarget(IRHIRenderTarget* pTarget)
{
	auto* pGL = static_cast<RHIRenderTargetGL*>(pTarget);
	if (!m_bValid || pGL == nullptr || m_bRenderTargetBound)
		return;

	gl::Int prevFBO = 0;
	gl::GetIntegerv(gl::FRAMEBUFFER_BINDING, &prevFBO);
	m_uSavedFBO       = static_cast<gl::Uint>(prevFBO);
	m_iSavedWinPixelW = m_iWinPixelW;
	m_iSavedWinPixelH = m_iWinPixelH;

	gl::BindFramebuffer(gl::FRAMEBUFFER, pGL->FBO());

	// ApplyViewport reads m_iWinPixelW/H for both the pre-transformed path and
	// the Y-flip; point them at the target so engine draws land correctly.
	m_iWinPixelW = static_cast<int>(pGL->Width());
	m_iWinPixelH = static_cast<int>(pGL->Height());
	gl::Viewport(0, 0, m_iWinPixelW, m_iWinPixelH);

	m_bRenderTargetBound = true;
}

void RHIDeviceGL::EndRenderTarget()
{
	if (!m_bRenderTargetBound)
		return;

	gl::BindFramebuffer(gl::FRAMEBUFFER, m_uSavedFBO);
	m_iWinPixelW = m_iSavedWinPixelW;
	m_iWinPixelH = m_iSavedWinPixelH;
	gl::Viewport(0, 0, m_iWinPixelW, m_iWinPixelH);
	m_bRenderTargetBound = false;
}

bool RHIDeviceGL::ReadCenterPixel(uint8_t rgbaOut[4])
{
	if (!m_bValid)
		return false;

	gl::ReadPixels(m_iWinPixelW / 2, m_iWinPixelH / 2, 1, 1, gl::RGBA, gl::UNSIGNED_BYTE, rgbaOut);
	return gl::GetError() == 0;
}

bool RHIDeviceGL::ReadRenderTargetPixel(IRHIRenderTarget* pTarget, int x, int y, uint8_t rgbaOut[4])
{
	auto* pGL = static_cast<RHIRenderTargetGL*>(pTarget);
	if (!m_bValid || pGL == nullptr)
		return false;

	gl::Int prevFBO = 0;
	gl::GetIntegerv(gl::FRAMEBUFFER_BINDING, &prevFBO);

	gl::BindFramebuffer(gl::FRAMEBUFFER, pGL->FBO());
	gl::ReadPixels(x, y, 1, 1, gl::RGBA, gl::UNSIGNED_BYTE, rgbaOut);
	const bool ok = gl::GetError() == 0;
	gl::BindFramebuffer(gl::FRAMEBUFFER, static_cast<gl::Uint>(prevFBO));
	return ok;
}

bool RHIDeviceGL::ReadRenderTargetRGBA(IRHIRenderTarget* pTarget, uint8_t* pRGBAOut)
{
	auto* pGL = static_cast<RHIRenderTargetGL*>(pTarget);
	if (!m_bValid || pGL == nullptr || pRGBAOut == nullptr)
		return false;

	gl::Int prevFBO = 0;
	gl::GetIntegerv(gl::FRAMEBUFFER_BINDING, &prevFBO);

	gl::BindFramebuffer(gl::FRAMEBUFFER, pGL->FBO());
	// RGBA8 rows are always 4-byte aligned, so the default pack alignment is fine.
	gl::ReadPixels(0, 0, static_cast<gl::Sizei>(pGL->Width()), static_cast<gl::Sizei>(pGL->Height()),
		gl::RGBA, gl::UNSIGNED_BYTE, pRGBAOut);
	const bool ok = gl::GetError() == 0;
	gl::BindFramebuffer(gl::FRAMEBUFFER, static_cast<gl::Uint>(prevFBO));
	return ok;
}

bool RHIDeviceGL::DumpFramePPM(const char* szPath)
{
	if (!m_bValid)
		return false;

	const int w = m_iWinPixelW, h = m_iWinPixelH;
	std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
	gl::ReadPixels(0, 0, w, h, gl::RGBA, gl::UNSIGNED_BYTE, rgba.data());
	if (gl::GetError() != 0)
		return false;

	FILE* pFile = fopen(szPath, "wb");
	if (pFile == nullptr)
		return false;

	fprintf(pFile, "P6\n%d %d\n255\n", w, h);
	// GL rows are bottom-up; PPM wants top-down.
	std::vector<uint8_t> row(static_cast<size_t>(w) * 3);
	for (int y = h - 1; y >= 0; --y)
	{
		const uint8_t* pSrc = rgba.data() + static_cast<size_t>(y) * w * 4;
		for (int x = 0; x < w; ++x)
		{
			row[x * 3 + 0] = pSrc[x * 4 + 0];
			row[x * 3 + 1] = pSrc[x * 4 + 1];
			row[x * 3 + 2] = pSrc[x * 4 + 2];
		}
		fwrite(row.data(), 1, row.size(), pFile);
	}
	fclose(pFile);
	return true;
}

// ============================================================================
// Resources
// ============================================================================

HRESULT RHIDeviceGL::CreateTexture(UINT width, UINT height, UINT levels, DWORD /*usage*/,
	D3DFORMAT format, D3DPOOL /*pool*/, IRHITexture** ppTexture)
{
	if (ppTexture == nullptr || width == 0 || height == 0)
		return RHI_E_FAIL;

	if (levels == 0) // full chain, D3D9 semantics
	{
		levels = 1;
		for (UINT w = width, h = height; w > 1 || h > 1; ++levels)
		{
			w = (w > 1) ? w / 2 : 1;
			h = (h > 1) ? h / 2 : 1;
		}
	}

	*ppTexture = new RHITextureGL(width, height, levels, format);
	return D3D_OK;
}

HRESULT RHIDeviceGL::CreateVertexBuffer(
	UINT length, DWORD /*usage*/, DWORD fvf, D3DPOOL /*pool*/, IRHIVertexBuffer** ppBuffer)
{
	if (ppBuffer == nullptr || length == 0)
		return RHI_E_FAIL;

	*ppBuffer = new RHIVertexBufferGL(length, fvf);
	return D3D_OK;
}

HRESULT RHIDeviceGL::CreateIndexBuffer(
	UINT length, DWORD /*usage*/, D3DFORMAT format, D3DPOOL /*pool*/, IRHIIndexBuffer** ppBuffer)
{
	if (ppBuffer == nullptr || length == 0)
		return RHI_E_FAIL;

	*ppBuffer = new RHIIndexBufferGL(length, format);
	return D3D_OK;
}

HRESULT RHIDeviceGL::SetTexture(DWORD stage, IRHITexture* pTexture)
{
	if (stage < MAX_GL_STAGES)
		m_apTextures[stage] = pTexture;
	return D3D_OK;
}

HRESULT RHIDeviceGL::SetStreamSource(
	UINT streamNumber, IRHIVertexBuffer* pBuffer, UINT offsetInBytes, UINT stride)
{
	m_nStreamOffset = offsetInBytes;
	m_nStreamStride = stride;
	return RHIDeviceNull::SetStreamSource(streamNumber, pBuffer, offsetInBytes, stride);
}

// ============================================================================
// Draw-time state application
// ============================================================================

void RHIDeviceGL::ApplyFixedState()
{
	DWORD value = 0;

	GetRenderState(D3DRS_ALPHABLENDENABLE, &value);
	if (value)
	{
		DWORD src = D3DBLEND_ONE, dst = D3DBLEND_ZERO;
		GetRenderState(D3DRS_SRCBLEND, &src);
		GetRenderState(D3DRS_DESTBLEND, &dst);
		gl::Enable(gl::BLEND);
		gl::BlendFunc(gltr::BlendFactor(src), gltr::BlendFactor(dst));
	}
	else
	{
		gl::Disable(gl::BLEND);
	}

	GetRenderState(D3DRS_ZENABLE, &value);
	if (value)
	{
		gl::Enable(gl::DEPTH_TEST);
		DWORD zfunc = D3DCMP_LESSEQUAL;
		GetRenderState(D3DRS_ZFUNC, &zfunc);
		gl::DepthFunc(gltr::CompareFunc(zfunc));
	}
	else
	{
		gl::Disable(gl::DEPTH_TEST);
	}

	GetRenderState(D3DRS_ZWRITEENABLE, &value);
	gl::DepthMask(value ? 1 : 0);

	// Both APIs map NDC +y to the top of the window, but D3D classifies
	// winding with y down and GL with y up: the same on-screen triangle is
	// D3D-CW exactly when it is GL-CCW. D3D's default front face is CW, so
	// GL front must be CW here for D3DCULL_CCW to cull the same triangles.
	GetRenderState(D3DRS_CULLMODE, &value);
	if (value == D3DCULL_NONE)
	{
		gl::Disable(gl::CULL_FACE);
	}
	else
	{
		gl::Enable(gl::CULL_FACE);
		gl::FrontFace(gl::CW);
		gl::CullFace(value == D3DCULL_CCW ? gl::BACK : gl::FRONT);
	}

	GetRenderState(D3DRS_FILLMODE, &value);
	gl::PolygonMode(gl::FRONT_AND_BACK, value == D3DFILL_WIREFRAME ? gl::LINE
			: (value == D3DFILL_POINT ? gl::POINT : gl::FILL));

	GetRenderState(D3DRS_SCISSORTESTENABLE, &value);
	if (value)
	{
		const RECT rc = ScissorRect();
		gl::Enable(gl::SCISSOR_TEST);
		gl::Scissor(rc.left, m_iWinPixelH - rc.bottom, rc.right - rc.left, rc.bottom - rc.top);
	}
	else
	{
		gl::Disable(gl::SCISSOR_TEST);
	}
}

void RHIDeviceGL::ApplyViewport(bool bPreTransformed)
{
	if (bPreTransformed)
	{
		// XYZRHW vertices are already in render-target pixels; D3D applies no
		// viewport transform to them, so map through the full window.
		gl::Viewport(0, 0, m_iWinPixelW, m_iWinPixelH);
		gl::DepthRange(0.0, 1.0);
		if (m_Locs.viewportSize >= 0)
			gl::Uniform2f(m_Locs.viewportSize, static_cast<float>(m_iWinPixelW),
				static_cast<float>(m_iWinPixelH));
		return;
	}

	D3DVIEWPORT9 vp = {};
	GetViewport(&vp);
	const gl::Int y =
		m_iWinPixelH - static_cast<gl::Int>(vp.Y) - static_cast<gl::Int>(vp.Height);
	gl::Viewport(static_cast<gl::Int>(vp.X), y, static_cast<gl::Sizei>(vp.Width),
		static_cast<gl::Sizei>(vp.Height));
	gl::DepthRange(vp.MinZ, vp.MaxZ);
}

void RHIDeviceGL::ApplyUniforms(const gltr::FVFLayout& layout)
{
	DWORD value = 0;

	gl::Uniform1i(m_Locs.preTransformed, layout.xyzrhw ? 1 : 0);
	gl::Uniform1i(m_Locs.hasNormal, layout.normalOffset >= 0 ? 1 : 0);
	gl::Uniform1i(m_Locs.hasColor, layout.colorOffset >= 0 ? 1 : 0);

	if (!layout.xyzrhw)
	{
		_D3DMATRIX world = {}, view = {}, proj = {};
		GetTransform(D3DTS_WORLD, &world);
		GetTransform(D3DTS_VIEW, &view);
		GetTransform(D3DTS_PROJECTION, &proj);

		// __Matrix44 shares the row-major D3D layout; uploading it without
		// transpose makes GL read the transpose, which is exactly what the
		// column-vector shader math needs (v * WVP == WVP^T * v).
		const __Matrix44& w = *reinterpret_cast<const __Matrix44*>(&world);
		const __Matrix44& v = *reinterpret_cast<const __Matrix44*>(&view);
		const __Matrix44& p = *reinterpret_cast<const __Matrix44*>(&proj);
		const __Matrix44 wv  = w * v;
		const __Matrix44 wvp = wv * p;

		gl::UniformMatrix4fv(m_Locs.wvp, 1, 0, reinterpret_cast<const float*>(&wvp));
		gl::UniformMatrix4fv(m_Locs.world, 1, 0, reinterpret_cast<const float*>(&w));
		gl::UniformMatrix4fv(m_Locs.wv, 1, 0, reinterpret_cast<const float*>(&wv));
	}

	// --- Lighting --------------------------------------------------------------
	GetRenderState(D3DRS_LIGHTING, &value);
	const bool bLighting = (value != 0);
	gl::Uniform1i(m_Locs.lighting, bLighting ? 1 : 0);

	if (bLighting && layout.normalOffset >= 0)
	{
		float ambient[4];
		GetRenderState(D3DRS_AMBIENT, &value);
		ColorToFloats(value, ambient);
		gl::Uniform4fv(m_Locs.globalAmbient, 1, ambient);

		gl::Uniform4fv(m_Locs.matDiffuse, 1, &m_Material.Diffuse.r);
		gl::Uniform4fv(m_Locs.matAmbient, 1, &m_Material.Ambient.r);
		gl::Uniform4fv(m_Locs.matEmissive, 1, &m_Material.Emissive.r);

		float pos[MAX_GL_LIGHTS * 4], dir[MAX_GL_LIGHTS * 4], diff[MAX_GL_LIGHTS * 4],
			amb[MAX_GL_LIGHTS * 4], att[MAX_GL_LIGHTS * 4];
		int count = 0;
		for (const auto& [index, enabled] : m_LightsEnabled)
		{
			if (!enabled || count >= MAX_GL_LIGHTS)
				continue;
			const auto it = m_Lights.find(index);
			if (it == m_Lights.end())
				continue;

			const _D3DLIGHT9& light = it->second;
			pos[count * 4 + 0]      = light.Position.x;
			pos[count * 4 + 1]      = light.Position.y;
			pos[count * 4 + 2]      = light.Position.z;
			pos[count * 4 + 3]      = light.Range;
			dir[count * 4 + 0]      = light.Direction.x;
			dir[count * 4 + 1]      = light.Direction.y;
			dir[count * 4 + 2]      = light.Direction.z;
			dir[count * 4 + 3]      = static_cast<float>(light.Type);
			std::memcpy(&diff[count * 4], &light.Diffuse.r, 4 * sizeof(float));
			std::memcpy(&amb[count * 4], &light.Ambient.r, 4 * sizeof(float));
			att[count * 4 + 0] = light.Attenuation0;
			att[count * 4 + 1] = light.Attenuation1;
			att[count * 4 + 2] = light.Attenuation2;
			att[count * 4 + 3] = 0.0f;
			++count;
		}

		gl::Uniform1i(m_Locs.lightCount, count);
		if (count > 0)
		{
			gl::Uniform4fv(m_Locs.lightPos, count, pos);
			gl::Uniform4fv(m_Locs.lightDir, count, dir);
			gl::Uniform4fv(m_Locs.lightDiffuse, count, diff);
			gl::Uniform4fv(m_Locs.lightAmbient, count, amb);
			gl::Uniform4fv(m_Locs.lightAtt, count, att);
		}
	}

	// --- Texture stage combiners -------------------------------------------------
	gl::Int colorOp[MAX_GL_STAGES], colorArg1[MAX_GL_STAGES], colorArg2[MAX_GL_STAGES];
	gl::Int alphaOp[MAX_GL_STAGES], alphaArg1[MAX_GL_STAGES], alphaArg2[MAX_GL_STAGES];
	gl::Int texBound[MAX_GL_STAGES];
	for (DWORD stage = 0; stage < MAX_GL_STAGES; ++stage)
	{
		GetTextureStageState(stage, D3DTSS_COLOROP, &value);
		colorOp[stage] = static_cast<gl::Int>(value);
		GetTextureStageState(stage, D3DTSS_COLORARG1, &value);
		colorArg1[stage] = static_cast<gl::Int>(value);
		GetTextureStageState(stage, D3DTSS_COLORARG2, &value);
		colorArg2[stage] = static_cast<gl::Int>(value);
		GetTextureStageState(stage, D3DTSS_ALPHAOP, &value);
		alphaOp[stage] = static_cast<gl::Int>(value);
		GetTextureStageState(stage, D3DTSS_ALPHAARG1, &value);
		alphaArg1[stage] = static_cast<gl::Int>(value);
		GetTextureStageState(stage, D3DTSS_ALPHAARG2, &value);
		alphaArg2[stage] = static_cast<gl::Int>(value);
		texBound[stage]  = (m_apTextures[stage] != nullptr) ? 1 : 0;
	}
	gl::Uniform1iv(m_Locs.colorOp, MAX_GL_STAGES, colorOp);
	gl::Uniform1iv(m_Locs.colorArg1, MAX_GL_STAGES, colorArg1);
	gl::Uniform1iv(m_Locs.colorArg2, MAX_GL_STAGES, colorArg2);
	gl::Uniform1iv(m_Locs.alphaOp, MAX_GL_STAGES, alphaOp);
	gl::Uniform1iv(m_Locs.alphaArg1, MAX_GL_STAGES, alphaArg1);
	gl::Uniform1iv(m_Locs.alphaArg2, MAX_GL_STAGES, alphaArg2);
	gl::Uniform1iv(m_Locs.texBound, MAX_GL_STAGES, texBound);

	float tfactor[4];
	GetRenderState(D3DRS_TEXTUREFACTOR, &value);
	ColorToFloats(value, tfactor);
	gl::Uniform4fv(m_Locs.tfactor, 1, tfactor);

	// --- Alpha test ---------------------------------------------------------------
	GetRenderState(D3DRS_ALPHATESTENABLE, &value);
	if (value)
	{
		DWORD func = D3DCMP_ALWAYS, ref = 0;
		GetRenderState(D3DRS_ALPHAFUNC, &func);
		GetRenderState(D3DRS_ALPHAREF, &ref);
		gl::Uniform1i(m_Locs.alphaTestFunc, static_cast<gl::Int>(func));
		gl::Uniform1f(m_Locs.alphaRef, static_cast<float>(ref & 0xFF) / 255.0f);
	}
	else
	{
		gl::Uniform1i(m_Locs.alphaTestFunc, 0);
	}

	// --- Fog -----------------------------------------------------------------------
	GetRenderState(D3DRS_FOGENABLE, &value);
	bool bFog = (value != 0);
	if (bFog)
	{
		// Only the linear mode the engine uses is implemented.
		DWORD tableMode = D3DFOG_NONE, vertexMode = D3DFOG_NONE;
		GetRenderState(D3DRS_FOGTABLEMODE, &tableMode);
		GetRenderState(D3DRS_FOGVERTEXMODE, &vertexMode);
		bFog = (tableMode == D3DFOG_LINEAR || vertexMode == D3DFOG_LINEAR);
	}
	gl::Uniform1i(m_Locs.fogEnable, bFog ? 1 : 0);
	if (bFog)
	{
		float fogColor[4];
		GetRenderState(D3DRS_FOGCOLOR, &value);
		ColorToFloats(value, fogColor);
		gl::Uniform4fv(m_Locs.fogColor, 1, fogColor);

		GetRenderState(D3DRS_FOGSTART, &value);
		gl::Uniform1f(m_Locs.fogStart, FloatBits(value));
		GetRenderState(D3DRS_FOGEND, &value);
		gl::Uniform1f(m_Locs.fogEnd, FloatBits(value));
	}
}

void RHIDeviceGL::ApplyTexturesAndSamplers()
{
	for (DWORD stage = 0; stage < MAX_GL_STAGES; ++stage)
	{
		gl::ActiveTexture(gl::TEXTURE0 + stage);
		if (m_apTextures[stage] != nullptr)
		{
			// All textures under this device are RHITextureGL.
			static_cast<RHITextureGL*>(m_apTextures[stage])->GLTexture(); // binds it
		}
		else
		{
			gl::BindTexture(gl::TEXTURE_2D, 0);
		}

		DWORD minF = D3DTEXF_POINT, magF = D3DTEXF_POINT, mipF = D3DTEXF_NONE;
		DWORD addrU = D3DTADDRESS_WRAP, addrV = D3DTADDRESS_WRAP, border = 0;
		GetSamplerState(stage, D3DSAMP_MINFILTER, &minF);
		GetSamplerState(stage, D3DSAMP_MAGFILTER, &magF);
		GetSamplerState(stage, D3DSAMP_MIPFILTER, &mipF);
		GetSamplerState(stage, D3DSAMP_ADDRESSU, &addrU);
		GetSamplerState(stage, D3DSAMP_ADDRESSV, &addrV);
		GetSamplerState(stage, D3DSAMP_BORDERCOLOR, &border);

		const bool bHasMips = (m_apTextures[stage] != nullptr)
							  && (m_apTextures[stage]->GetLevelCount() > 1);
		const gl::Uint sampler = m_auSamplers[stage];
		gl::SamplerParameteri(sampler, gl::TEXTURE_MIN_FILTER,
			static_cast<gl::Int>(gltr::MinFilter(minF, mipF, bHasMips)));
		gl::SamplerParameteri(
			sampler, gl::TEXTURE_MAG_FILTER, static_cast<gl::Int>(gltr::MagFilter(magF)));
		gl::SamplerParameteri(
			sampler, gl::TEXTURE_WRAP_S, static_cast<gl::Int>(gltr::WrapMode(addrU)));
		gl::SamplerParameteri(
			sampler, gl::TEXTURE_WRAP_T, static_cast<gl::Int>(gltr::WrapMode(addrV)));

		float borderRgba[4];
		ColorToFloats(border, borderRgba);
		gl::SamplerParameterfv(sampler, gl::TEXTURE_BORDER_COLOR, borderRgba);

		gl::BindSampler(stage, sampler);
	}
	gl::ActiveTexture(gl::TEXTURE0);
}

void RHIDeviceGL::SetupVertexAttribs(const gltr::FVFLayout& layout, intptr_t baseOffset)
{
	const auto Offset = [baseOffset](int fieldOffset)
	{ return reinterpret_cast<const void*>(baseOffset + fieldOffset); };

	gl::EnableVertexAttribArray(0);
	gl::VertexAttribPointer(0, layout.xyzrhw ? 4 : 3, gl::FLOAT, 0, layout.stride,
		Offset(layout.posOffset));

	if (layout.normalOffset >= 0)
	{
		gl::EnableVertexAttribArray(1);
		gl::VertexAttribPointer(1, 3, gl::FLOAT, 0, layout.stride, Offset(layout.normalOffset));
	}
	else
	{
		gl::DisableVertexAttribArray(1);
	}

	if (layout.colorOffset >= 0)
	{
		// D3DCOLOR is BGRA in memory; size = GL_BGRA gives the hardware swizzle
		// (ARB_vertex_array_bgra, core since 3.2).
		gl::EnableVertexAttribArray(2);
		gl::VertexAttribPointer(2, static_cast<gl::Int>(gl::BGRA), gl::UNSIGNED_BYTE, 1,
			layout.stride, Offset(layout.colorOffset));
	}
	else
	{
		gl::DisableVertexAttribArray(2);
	}

	for (int uv = 0; uv < 2; ++uv)
	{
		const gl::Uint attrib = 3 + static_cast<gl::Uint>(uv);
		if (uv < layout.uvCount)
		{
			gl::EnableVertexAttribArray(attrib);
			gl::VertexAttribPointer(
				attrib, 2, gl::FLOAT, 0, layout.stride, Offset(layout.uvOffset[uv]));
		}
		else
		{
			gl::DisableVertexAttribArray(attrib);
		}
	}
}

bool RHIDeviceGL::BeginDraw(const gltr::FVFLayout& layout)
{
	if (!m_bValid || !layout.valid)
		return false;

	gl::UseProgram(m_uProgram);
	ApplyFixedState();
	ApplyViewport(layout.xyzrhw);
	ApplyUniforms(layout);
	ApplyTexturesAndSamplers();
	return true;
}

// ============================================================================
// Draws
// ============================================================================

HRESULT RHIDeviceGL::DrawPrimitive(
	D3DPRIMITIVETYPE primitiveType, UINT startVertex, UINT primitiveCount)
{
	const gltr::FVFLayout layout = gltr::ParseFVF(m_dwFVF);
	auto* pVB                    = static_cast<RHIVertexBufferGL*>(m_pBoundVB);
	if (pVB == nullptr || !BeginDraw(layout))
		return D3D_OK;

	pVB->GLBuffer(); // binds + uploads if dirty
	SetupVertexAttribs(layout, static_cast<intptr_t>(m_nStreamOffset));

	gl::DrawArrays(gltr::PrimitiveMode(primitiveType), static_cast<gl::Int>(startVertex),
		static_cast<gl::Sizei>(gltr::PrimitiveElementCount(primitiveType, primitiveCount)));

	++m_nDrawCalls;
	return D3D_OK;
}

HRESULT RHIDeviceGL::DrawIndexedPrimitive(D3DPRIMITIVETYPE primitiveType, INT baseVertexIndex,
	UINT /*minVertexIndex*/, UINT /*numVertices*/, UINT startIndex, UINT primitiveCount)
{
	const gltr::FVFLayout layout = gltr::ParseFVF(m_dwFVF);
	auto* pVB                    = static_cast<RHIVertexBufferGL*>(m_pBoundVB);
	auto* pIB                    = static_cast<RHIIndexBufferGL*>(m_pBoundIB);
	if (pVB == nullptr || pIB == nullptr || !BeginDraw(layout))
		return D3D_OK;

	pVB->GLBuffer();
	SetupVertexAttribs(layout, static_cast<intptr_t>(m_nStreamOffset));
	pIB->GLBuffer();

	const UINT indexCount = gltr::PrimitiveElementCount(primitiveType, primitiveCount);
	gl::DrawElementsBaseVertex(gltr::PrimitiveMode(primitiveType),
		static_cast<gl::Sizei>(indexCount), gl::UNSIGNED_SHORT,
		reinterpret_cast<const void*>(static_cast<intptr_t>(startIndex) * 2), baseVertexIndex);

	++m_nDrawCalls;
	return D3D_OK;
}

HRESULT RHIDeviceGL::DrawPrimitiveUP(D3DPRIMITIVETYPE primitiveType, UINT primitiveCount,
	const void* pVertexData, UINT vertexStride)
{
	if (pVertexData == nullptr)
		return RHI_E_FAIL;

	const gltr::FVFLayout layout = gltr::ParseFVF(m_dwFVF);
	if (!BeginDraw(layout))
		return D3D_OK;

	const UINT vertexCount = gltr::PrimitiveElementCount(primitiveType, primitiveCount);
	gl::BindBuffer(gl::ARRAY_BUFFER, m_uStreamVB);
	gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::Sizeiptr>(vertexCount) * vertexStride,
		pVertexData, gl::STREAM_DRAW);
	SetupVertexAttribs(layout, 0);

	gl::DrawArrays(
		gltr::PrimitiveMode(primitiveType), 0, static_cast<gl::Sizei>(vertexCount));

	++m_nDrawCalls;
	return D3D_OK;
}

HRESULT RHIDeviceGL::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE primitiveType, UINT minVertexIndex,
	UINT numVertices, UINT primitiveCount, const void* pIndexData, D3DFORMAT /*indexFormat*/,
	const void* pVertexData, UINT vertexStride)
{
	if (pIndexData == nullptr || pVertexData == nullptr)
		return RHI_E_FAIL;

	const gltr::FVFLayout layout = gltr::ParseFVF(m_dwFVF);
	if (!BeginDraw(layout))
		return D3D_OK;

	const UINT indexCount = gltr::PrimitiveElementCount(primitiveType, primitiveCount);

	gl::BindBuffer(gl::ARRAY_BUFFER, m_uStreamVB);
	gl::BufferData(gl::ARRAY_BUFFER,
		static_cast<gl::Sizeiptr>(minVertexIndex + numVertices) * vertexStride, pVertexData,
		gl::STREAM_DRAW);
	SetupVertexAttribs(layout, 0);

	gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, m_uStreamIB);
	gl::BufferData(gl::ELEMENT_ARRAY_BUFFER, static_cast<gl::Sizeiptr>(indexCount) * 2, pIndexData,
		gl::STREAM_DRAW);

	gl::DrawElements(
		gltr::PrimitiveMode(primitiveType), static_cast<gl::Sizei>(indexCount), gl::UNSIGNED_SHORT,
		nullptr);

	++m_nDrawCalls;
	return D3D_OK;
}
