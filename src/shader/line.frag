#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"

layout (location = 0) in vec4 frag_color;
layout (location = 1) in vec4 frag_pos;
layout (location = 2) in vec2 out_x;
layout (location = 3) in vec2 out_y;

layout (location = 0) out vec4 FragColor;

void main()
{
	float t = 5*length(out_x);
	float s = 5*length(out_y);
	FragColor = frag_color;
}
