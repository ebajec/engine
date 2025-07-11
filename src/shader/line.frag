#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"

layout (location = 0) in vec4 frag_color;
layout (location = 1) in vec4 frag_pos;
layout (location = 2) flat in vec2 dir;
layout (location = 3) flat in vec2 center;

layout (location = 0) out vec4 FragColor;

void main()
{
	vec2 p = frag_pos.xy - center;

	vec4 color = frag_color;

	//if (dot(p,dir) > 0 && length(p) > 0.1)
	//	discard;

	FragColor = color;
}
