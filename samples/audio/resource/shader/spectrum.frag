#version 430 core
#include "core/shader/frame.glsl"

layout (location = 0) in vec4 in_color;
layout (location = 1) in vec2 in_pos;
layout (location = 2) flat in float in_xc;
layout (location = 3) flat in int in_bins;
layout (location = 0) out vec4 out_color;

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require

layout (buffer_reference, std430, buffer_reference_align = 16) readonly buffer Spectrum
{
	float data[];
};

layout (push_constant) uniform PC {
	Spectrum spec;
	int bins;
} pc;

void main()
{
	vec3 rgb = mix(vec3(0.1,0.1,1), vec3(1,0.3,0.3), abs(in_pos.y));

	float dx = (in_pos.x - in_xc) * in_bins;
	float fade = 1 - exp(-(dx * dx));

	out_color = vec4(fade*rgb, 1);
}
