#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"

//-------------------------------------------------------------------------------------
// Vert

layout (location = 0) in vec2 pos;
layout (location = 1) in vec2 uv;

layout (location = 0) out vec2 frag_pos;
layout (location = 1) out vec2 frag_uv;

void main()
{
	float t = u_frame.t;
	mat4 pv = u_frame.pv;

	vec4 wpos = vec4(pos.x, pos.y, 0, 1);
	frag_pos = pos;
	frag_uv = uv;

	gl_Position = pv*wpos;
}

