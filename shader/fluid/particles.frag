#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../core/frame.glsl"

layout (location = 0) in vec2 in_pos;
layout (location = 1) flat in vec2 in_center;
layout (location = 2) in vec4 in_color;
layout (location = 3) in vec2 in_vel;

layout (location = 0) out vec4 out_color;

void main()
{
	vec2 r = in_pos - in_center;

//	float f = dot(in_vel, r);
//	float den = length(in_vel) * length(r);; 
//
//	if (den > 1e-3)
//		f /= den;

	float f = 1.f;

	float a = exp(-40*dot(r,r)) * f;
	if (a < 0.7)
		discard;

	out_color = vec4(in_color.rgb, 1.f);
}
