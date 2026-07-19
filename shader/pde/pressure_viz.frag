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

	vec2 h = vec2(1.f)/size;

	uint L = 0;
	vec4 tex = texture(u_tex, in_uv);

	ivec2 texc = ivec2(in_uv * vec2(size));

	ivec2 lim = size - ivec2(1);

	float c = texelFetch(u_tex, texc, 0).r;

	vec3 rgb;
	if (true) {
		float l = texelFetch(u_tex, clamp(texc + ivec2(-1, 0), ivec2(0), lim), 0).r;
		float r = texelFetch(u_tex, clamp(texc + ivec2(1, 0), ivec2(0), lim), 0).r;
		float b = texelFetch(u_tex, clamp(texc + ivec2(0, -1), ivec2(0), lim), 0).r;
		float t = texelFetch(u_tex, clamp(texc + ivec2(0, 1), ivec2(0), lim), 0).r;

		vec2 grad = 0.5 * vec2(r - l, t - b); 

		float k = length(grad);
		rgb = 10*vec3(-grad.y, grad.y, 0);
	} else {
		rgb = vec3(c, 0, -c);
	}

	if (in_uv.x < 0.f || in_uv.y < 0.f || in_uv.x > 1.f || in_uv.y > 1.f) {
		FragColor = vec4(0.5);
	} else {
		FragColor = vec4(rgb,1); 
	}
}

