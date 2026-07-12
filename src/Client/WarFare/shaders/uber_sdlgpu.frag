#version 450

// SDL_GPU uber fragment shader (docs/PORT_POSIX_PLAN.md, T6b.1): direct port
// of RHIDeviceGL's fragment uber-shader. Stage ops/args arrive as D3D token
// values so the mapping stays greppable against d3d9types.h. SDL_GPU
// fragment resources: sampled textures = set 2, uniform buffers = set 3.

layout(location = 0) in vec4  vColor;
layout(location = 1) in vec2  vUV0;
layout(location = 2) in vec2  vUV1;
layout(location = 3) in float vFogDepth;

layout(set = 2, binding = 0) uniform sampler2D uTex0;
layout(set = 2, binding = 1) uniform sampler2D uTex1;
layout(set = 2, binding = 2) uniform sampler2D uTex2;

layout(set = 3, binding = 0, std140) uniform FSUniforms
{
	ivec4 stageColor[3]; // x = D3DTOP op, y = arg1, z = arg2, w = texBound
	ivec4 stageAlpha[3]; // x = D3DTOP op, y = arg1, z = arg2
	vec4 tfactor;
	vec4 fogColor;
	vec4 fogParams;      // x = start, y = end, z = enable
	vec4 alphaTest;      // x = D3DCMP func (0 = off), y = ref
} u;

layout(location = 0) out vec4 oColor;

vec4 SelectArg(int arg, vec4 current, vec4 tex)
{
	if (arg == 0) return vColor;    // D3DTA_DIFFUSE
	if (arg == 1) return current;   // D3DTA_CURRENT
	if (arg == 2) return tex;       // D3DTA_TEXTURE
	if (arg == 3) return u.tfactor; // D3DTA_TFACTOR
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
	vec4 c1     = SelectArg(u.stageColor[i].y, current, tex);
	vec4 c2     = SelectArg(u.stageColor[i].z, current, tex);
	vec4 outCol = current;
	outCol.rgb  = CombineRGB(u.stageColor[i].x, c1.rgb, c2.rgb);
	if (u.stageAlpha[i].x != 1) // D3DTOP_DISABLE keeps the current alpha
	{
		vec4 a1  = SelectArg(u.stageAlpha[i].y, current, tex);
		vec4 a2  = SelectArg(u.stageAlpha[i].z, current, tex);
		outCol.a = CombineA(u.stageAlpha[i].x, a1.a, a2.a);
	}
	return outCol;
}

bool AlphaTestPasses(float a)
{
	int func  = int(u.alphaTest.x);
	float ref = u.alphaTest.y;
	if (func == 1) return false;         // NEVER
	if (func == 2) return a <  ref;      // LESS
	if (func == 3) return a == ref;      // EQUAL
	if (func == 4) return a <= ref;      // LESSEQUAL
	if (func == 5) return a >  ref;      // GREATER
	if (func == 6) return a != ref;      // NOTEQUAL
	if (func == 7) return a >= ref;      // GREATEREQUAL
	return true;                         // ALWAYS
}

void main()
{
	// Unbound texture stages sample as opaque white so MODULATE passes
	// diffuse through (matches how the engine configures unused stages).
	vec4 tex0 = (u.stageColor[0].w != 0) ? texture(uTex0, vUV0) : vec4(1.0);
	vec4 tex1 = (u.stageColor[1].w != 0) ? texture(uTex1, vUV1) : vec4(1.0);
	vec4 tex2 = (u.stageColor[2].w != 0) ? texture(uTex2, vUV1) : vec4(1.0);

	vec4 current = vColor;
	bool stop    = (u.stageColor[0].x == 1); // D3DTOP_DISABLE ends the chain
	if (!stop)
	{
		current = RunStage(0, current, tex0);
		stop    = (u.stageColor[1].x == 1);
	}
	if (!stop)
	{
		current = RunStage(1, current, tex1);
		stop    = (u.stageColor[2].x == 1);
	}
	if (!stop)
	{
		current = RunStage(2, current, tex2);
	}

	if (int(u.alphaTest.x) != 0 && !AlphaTestPasses(current.a))
		discard;

	if (u.fogParams.z != 0.0)
	{
		float f = clamp(
			(u.fogParams.y - vFogDepth) / max(u.fogParams.y - u.fogParams.x, 1e-5), 0.0, 1.0);
		current.rgb = mix(u.fogColor.rgb, current.rgb, f);
	}

	oColor = current;
}
