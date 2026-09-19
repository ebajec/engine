#version 430 core
#extension GL_GOOGLE_include_directive : require

#include "core/shader/frame.glsl"
#include "core/shader/jet_palette.glsl"

layout (set = PER_DRAW_SET, binding = 0) uniform sampler2D u_tex;

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec2 in_uv;

layout (location = 0) out vec4 FragColor;

float get_p(ivec2 p)
{
	ivec2 size = textureSize(u_tex,0);
	if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, size)))
		return 0.f;
	return texelFetch(u_tex, p, 0).r;
}

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
	if (false) {
		float l = get_p(texc + ivec2(-1, 0));
		float r = get_p(texc + ivec2(1, 0));
		float b = get_p(texc + ivec2(0, -1));
		float t = get_p(texc + ivec2(0, 1));

		vec2 grad = 0.5 * vec2(r - l, t - b); 

		float k = grad.y;
		//rgb = 10*vec3(-grad.y, grad.y, 0);
		rgb = vec3(k, -k, 0);
	} else {
		//rgb = vec3(c, c, -c);
		vec3 jet = jet_palette(abs(0.05*c));
		rgb = jet;
	}

	if (in_uv.x < 0.f || in_uv.y < 0.f || in_uv.x > 1.f || in_uv.y > 1.f) {
		FragColor = vec4(0.5);
	} else {
		FragColor = vec4(rgb,1); 
	}
}

