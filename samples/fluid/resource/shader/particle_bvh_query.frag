#version 460 core
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require

#include "core/shader/frame.glsl"
#include "fluid/shader/fluid_particle.glsl"

#define MAX_DEPTH 6

struct AABB2
{
	vec2 min;
	vec2 max;
};

layout (buffer_reference, std430, buffer_reference_align = 16) readonly buffer BVH
{
	uint elem_count;
	uint internal_size;
	uint padding[2];
	AABB2 boxes[];
};

layout (buffer_reference, std430, buffer_reference_align = 16) readonly buffer Particles
{
	FluidParticle data[];
};

layout (push_constant) uniform PC
{
	vec2 size;
	BVH bvh;
	Particles parts;
} pc;

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec2 in_uv;
layout (location = 0) out vec4 out_color;

float dist2_box(AABB2 b, vec2 p)
{
	float s = 0;
	for (uint i = 0; i < 2; ++i) {
		float d = max(max(b.min[i] - p[i], 0.f), p[i] - b.max[i]);
		s += d * d;
	}
	return s;
}

float min_dist(vec2 p_search, float d_search)
{
	uint offsets[MAX_DEPTH];
	uint stack[MAX_DEPTH];
	uint indices[MAX_DEPTH];
	uint sp = 0;

	// always a power of two
	const uint factor = pc.bvh.internal_size;

	uint count = pc.bvh.elem_count;
	uint total = 0;
	for (int i = 0; i < MAX_DEPTH && count > 1; ++i) {
		count = (count + factor - 1) / factor;
		offsets[i] = total;
		indices[i] = 0;
		total += count;
		++sp;
	}

	uint root = sp;
	sp = root - 1;
	stack[sp] = 0x1;

	float value = 100.f;

	uint hit_ctr = 0;

	uint offset = total;
	while (sp < root){
		if (stack[sp] != 0) {
			// index of the current node in the parent's children.
			// cleared upon visit
			int msb = findMSB(stack[sp]);
			stack[sp] &= ~(1 << msb);

			uint child_idx = factor * (indices[sp] + msb);

			if (sp > 0) {
				--sp;
				indices[sp] = child_idx;

				uint start = offsets[sp] + child_idx;
				uint end = min(start + factor, offsets[sp + 1]);

				for (uint i = start; i < end; ++i) {
					bool hit = dist2_box(pc.bvh.boxes[i], p_search) < d_search;
					if (hit) {
						stack[sp] |= 1 << (i - start);
					}
				}
			} else {
				// We are in a leaf node

				uint start = child_idx;
				uint end = min(child_idx + factor, pc.bvh.elem_count);

				for (uint i = start; i < end; ++i) {
					vec2 r = pc.parts.data[i].pos - p_search;
					bool hit = dot(r,r) < d_search;
					if (hit) {
						++hit_ctr;
						value = min(value, dot(r,r));
					}
				}
			}
		} else {
			++sp;
		}
	}

	return bool(hit_ctr) ? value - 0.1 : 0;
}

void main()
{
	vec2 p_search = pc.size * in_uv;
	float d_search = 1.0f;

	float value = min_dist(p_search, d_search);

	vec3 rgb = vec3(value, 0, -value);
	out_color = vec4(rgb, 1.f);
}
