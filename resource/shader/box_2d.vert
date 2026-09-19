#version 460 core
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require

#include "core/shader/frame.glsl"

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

layout (push_constant) uniform PC
{
	mat3x2 world;
	uint level;
	uint offset;
	BVH bvh;
} pc;

void main()
{
	uint idx = gl_InstanceIndex;
	AABB2 box = pc.bvh.boxes[pc.offset + idx];

	vec2 corners[4] = {
		vec2(box.min.x, box.min.y),
		vec2(box.max.x, box.min.y),
		vec2(box.max.x, box.max.y),
		vec2(box.min.x, box.max.y)
	};

	uint vtx = gl_VertexIndex & 0x3;

	vec2 pos = corners[vtx];

	gl_Position = u_view.pv * vec4(pc.world * vec3(pos, 1), 0, 1);
}
