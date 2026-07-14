
#version 430 core
#extension GL_GOOGLE_include_directive : require

#include "../core/frame.glsl"
#include "../core/jet_palette.glsl"

layout (set = PER_DRAW_SET, binding = 0) uniform sampler2D u_tex;

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec2 in_uv;

layout (location = 0) out vec4 FragColor;

void main()
{
	ivec2 size = textureSize(u_tex, 0);

	uint L = 0;
	vec4 tex = textureLod(u_tex, in_uv, 0);

	float r = 400*abs(tex.r);
	
	vec3 rgb = jet_palette(r);

	if (in_uv.x < 0.f || in_uv.y < 0.f || in_uv.x > 1.f || in_uv.y > 1.f) {
		FragColor = vec4(0.5);
	} else {
		FragColor = vec4(rgb,1); 
	}
}

