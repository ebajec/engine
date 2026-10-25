#version 430 core
#include "core/shader/frame.glsl"
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require

layout (location = 0) out vec4 out_color;
layout (location = 1) out vec2 out_pos;
layout (location = 2) flat out float out_xc;
layout (location = 3) flat out int out_bins;

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
	int idx = clamp(gl_InstanceIndex - (gl_VertexIndex & 0x1), 0, 2*pc.bins);;
	int f_idx = idx < pc.bins ? pc.bins - 1 - idx : idx - pc.bins; 

	float bottom = bool(gl_VertexIndex / 2) ? -1 : 1;

	float amp = pc.spec.data[f_idx];

	float x = (float(idx - pc.bins) + 0.5) / pc.bins;
	float y = 0.5 * bottom*amp;

	vec2 pos = vec2(x,y);
	out_pos = pos;
	out_xc = (float(gl_InstanceIndex - pc.bins)) / pc.bins;
	out_bins = pc.bins;

	gl_Position = u_pass.pv * vec4(pos, 0, 1);
}

