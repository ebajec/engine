#version 460 core
#extension GL_GOOGLE_include_directive : require

layout (location = 0) in vec2 in_pos;
layout (location = 1) flat in vec2 in_center;
layout (location = 2) flat in uint in_idx;
layout (location = 3) flat in vec2 in_w;

layout (location = 0) out vec4 out_color;
#include "test_sort/shader/test_defs.glsl"

void main()
{
	vec4 color = unpackUnorm4x8(u_color);

	const vec2 buf = vec2(0.05*min(in_w.x, in_w.y));
	const bool is_edge = any(greaterThan(abs(in_pos - in_center), in_w - buf));
	out_color = color;
}
