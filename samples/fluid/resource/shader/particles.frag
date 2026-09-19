#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "core/shader/frame.glsl"

layout (location = 0) in vec2 in_pos;
layout (location = 1) flat in vec2 in_center;
layout (location = 2) in vec4 in_color;
layout (location = 3) in vec2 in_vel;
layout (location = 4) flat in uint in_idx;
layout (location = 5) flat in float in_w;

layout (location = 0) out vec4 out_color;

// bits of the key that span one full hue cycle
const uint MORTON_BITS = 8u;

vec3 morton_palette(uint code)
{
	const uint mask = (1u << MORTON_BITS) - 1u;
	float t = float((code >> 5) & mask) / float(mask + 1u);
	return 0.5 + 0.5*cos(6.28318530718*(t + vec3(0.0, 0.33, 0.67)));
}

uint pcg_hash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

void main()
{
	vec2 r = in_pos - in_center;

	float f = 0.1 + 0.01*length(in_vel);

	vec4 c = vec4(morton_palette(in_idx), f);
	//c = vec4(vec3(f), 1);

	if (dot(r,r) > in_w*in_w)
		discard;

	out_color = c;
}
