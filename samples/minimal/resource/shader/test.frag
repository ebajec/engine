#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "core/shader/frame.glsl"

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec2 in_uv;

layout (location = 0) out vec4 out_color;

vec3 hsv2rgb(vec3 c)
{
    vec3 p = abs(fract(c.x + vec3(0.0, 2.0/3.0, 1.0/3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

void main()
{
	float tht = atan(1 - 2 * in_uv.x, 1 - 2 * in_uv.y);

	float t = float(u_frame.t_sec & 0x3) + float(u_frame.t_fract);

	vec3 rgb = hsv2rgb(vec3(tht / 6.28 + fract(0.5*t), 1, length(in_pos)));
	out_color = vec4(rgb, 1);
}
