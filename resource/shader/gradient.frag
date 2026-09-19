#version 430 core
#extension GL_GOOGLE_include_directive : require

#include "core/shader/frame.glsl"

layout (set = PER_DRAW_SET, binding = 0) uniform sampler2D u_tex;

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec2 in_uv;

layout (location = 0) out vec4 FragColor;

void main()
{
	vec2 size = vec2(textureSize(u_tex, 0));
	vec2 h = 1.0f/size;

	vec4 l = texture(u_tex, in_uv + vec2(-h.x, 0));
	vec4 r = texture(u_tex, in_uv + vec2(h.x, 0));
	vec4 b = texture(u_tex, in_uv + vec2(0, -h.y));
	vec4 t = texture(u_tex, in_uv + vec2(0, h.y));

	vec2 grad[4];

	for (int i = 0; i < 4; ++i) {
		grad[i] = vec2(
			(r[i] - l[i]) / (2 * h.x),
			(t[i] - b[i]) / (2 * h.y)
		); 
	}

	float c = texture(u_tex, in_uv).r;

	vec2 d = grad[0];

	FragColor = vec4(d.y, -d.y, 0.1*c, 1.f);
}

