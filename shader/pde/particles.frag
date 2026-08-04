#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../core/frame.glsl"

layout (location = 0) in vec2 in_pos;
layout (location = 1) flat in vec2 in_center;
layout (location = 2) in vec4 in_color;

layout (location = 0) out vec4 out_color;

void main()
{
	vec2 r = in_pos - in_center;
	float a = exp(-4*dot(r,r));
	out_color = vec4(in_color.rgb, a);
}
