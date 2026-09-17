#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "core/frame.glsl"

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec2 in_uv;

layout (location = 0) out vec4 out_color;

void main()
{
	out_color = vec4(length(in_pos), 0, 0, 1);
}
