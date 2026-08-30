#version 430 core
#extension GL_GOOGLE_include_directive : require

#include "../core/frame.glsl"

layout (set = PER_DRAW_SET, binding = 0) uniform sampler2D u_tex;

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec2 in_uv;

layout (location = 0) out vec4 FragColor;

void main()
{
	ivec2 size = textureSize(u_tex, 0);
	vec4 tex = textureLod(u_tex, in_uv, 0);
	vec4 color = 0.2*vec4(tex.r, -tex.r, 0, 1);

	vec2 f = fract(in_uv * vec2(size));
	vec2 d = abs(f - vec2(0.5));
	FragColor = any(greaterThan(d, vec2(0.45))) ? vec4(0) : color; 
}


