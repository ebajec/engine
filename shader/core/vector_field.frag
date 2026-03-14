#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"

layout (location = 0) in vec3 in_pos;
layout (location = 1) in vec4 in_color;

layout (location = 0) out vec4 FragColor;

void main()
{
	FragColor = vec4(in_color.xyz, 1);
}
