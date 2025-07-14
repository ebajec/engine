#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"

//------------------------------------------------------------------------------
// Vert

layout (binding = 0) uniform sampler2D u_tex;

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 uv;
layout (location = 2) in vec3 normal;

layout (location = 0) out vec3 frag_pos;
layout (location = 1) out vec2 frag_uv;
layout (location = 2) out vec3 frag_normal;

void main()
{
	float t = u_frame.t;
	mat4 pv = u_frame.pv;

	vec4 wpos = vec4(pos, 1);
	vec4 n = vec4(normal,0);

	frag_pos = pos;
	frag_uv = uv;
	frag_normal = n.xyz;

	gl_Position = (pv*wpos);
}

