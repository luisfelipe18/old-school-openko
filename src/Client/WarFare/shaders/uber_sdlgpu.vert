#version 450

// SDL_GPU uber vertex shader (docs/PORT_POSIX_PLAN.md, T6b.1): a direct port
// of RHIDeviceGL's fixed-function uber-shader to Vulkan GLSL, compiled
// offline to SPIR-V (Vulkan) and MSL (Metal) by build_shaders.sh.
//
// Differences from the GL twin, all imposed by the target APIs:
//  - No depth remap: SDL_GPU clip space keeps z in [0,1] like D3D (the GL
//    shader had to remap to [-1,1]).
//  - The D3DCOLOR vertex color arrives as UBYTE4_NORM (no BGRA vertex format
//    in SDL_GPU), so the B<->R swizzle happens here instead of in the vertex
//    layout.
//  - Uniforms live in one std140 block (SDL_GPU vertex uniforms = set 1).
//  - Every attribute is always fed by the pipeline (Vulkan requires it);
//    absent ones alias offset 0 of the vertex and are ignored via flags.

layout(location = 0) in vec4 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aUV0;
layout(location = 4) in vec2 aUV1;

layout(set = 1, binding = 0, std140) uniform VSUniforms
{
	mat4 wvp;
	mat4 world;
	mat4 wv;
	vec4 viewportSize;   // x = width, y = height, z = preTransformed flag
	ivec4 flags;         // x = hasNormal, y = hasColor, z = lighting, w = lightCount
	vec4 globalAmbient;
	vec4 matDiffuse;
	vec4 matAmbient;
	vec4 matEmissive;
	vec4 lightPos[8];     // xyz + range
	vec4 lightDir[8];     // xyz + D3DLIGHTTYPE (1 = point, 3 = directional)
	vec4 lightDiffuse[8];
	vec4 lightAmbient[8];
	vec4 lightAtt[8];     // attenuation0/1/2
} u;

layout(location = 0) out vec4  vColor;
layout(location = 1) out vec2  vUV0;
layout(location = 2) out vec2  vUV1;
layout(location = 3) out float vFogDepth;

void main()
{
	if (u.viewportSize.z != 0.0)
	{
		// XYZRHW: x/y in render-target pixels, z already in [0,1].
		float x     = aPos.x * 2.0 / u.viewportSize.x - 1.0;
		float y     = 1.0 - aPos.y * 2.0 / u.viewportSize.y;
		gl_Position = vec4(x, y, aPos.z, 1.0);
		vFogDepth   = 0.0;
	}
	else
	{
		gl_Position = u.wvp * vec4(aPos.xyz, 1.0);
		vFogDepth   = (u.wv * vec4(aPos.xyz, 1.0)).z;
	}
	gl_PointSize = 1.0; // required when the topology is a point list

	// D3DCOLOR is B,G,R,A in memory; UBYTE4_NORM loads that as .xyzw.
	vec4 base = (u.flags.y != 0) ? aColor.zyxw : vec4(1.0);

	// D3D9 per-vertex lighting; the engine feeds world-space lights.
	if (u.flags.z != 0 && u.flags.x != 0 && u.viewportSize.z == 0.0)
	{
		vec3 n    = normalize(mat3(u.world) * aNormal);
		vec3 wpos = (u.world * vec4(aPos.xyz, 1.0)).xyz;
		vec3 diff = vec3(0.0);
		vec3 amb  = u.globalAmbient.rgb;
		for (int i = 0; i < u.flags.w; ++i)
		{
			vec3 l;
			float atten = 1.0;
			if (u.lightDir[i].w > 2.5) // directional
			{
				l = -normalize(u.lightDir[i].xyz);
			}
			else // point
			{
				vec3 d     = u.lightPos[i].xyz - wpos;
				float dist = length(d);
				if (dist > u.lightPos[i].w)
					continue;
				l     = d / max(dist, 1e-5);
				atten = 1.0
						/ max(u.lightAtt[i].x + u.lightAtt[i].y * dist
								  + u.lightAtt[i].z * dist * dist,
							1e-5);
			}
			diff += u.lightDiffuse[i].rgb * max(dot(n, l), 0.0) * atten;
			amb  += u.lightAmbient[i].rgb * atten;
		}
		vec3 lit = u.matEmissive.rgb + u.matAmbient.rgb * amb + u.matDiffuse.rgb * diff;
		base     = vec4(clamp(lit, 0.0, 1.0), u.matDiffuse.a);
	}

	vColor = base;
	vUV0   = aUV0;
	vUV1   = aUV1;
}
