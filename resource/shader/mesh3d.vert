#version 460 core
#extension GL_GOOGLE_include_directive : require
#include "core/shader/core.glsl"
#include "core/shader/common.glsl"

layout (location = 0) out vec3 out_pos;
layout (location = 1) out vec2 out_uv;
layout (location = 2) out vec3 out_normal;
layout (location = 3) flat out uint out_tex_id;
layout (location = 4) flat out uint out_inst_id;

struct MeshInstance
{
	uint tex_id;
	uint transform_id;
};

layout (buffer_reference, std430, buffer_reference_align = 16) readonly buffer MeshInstanceBuffer
{
	MeshInstance data[];
};

layout (push_constant) uniform PC {
	MeshInstanceBuffer inst;
} pc;

mat3 cofactor_signed(mat3 m)
{
    vec3 c0 = cross(m[1], m[2]);
    float s = sign(dot(m[0], c0)); // det(m) = dot(m[0], cross(m[1], m[2]))
    return s * mat3(c0, cross(m[2], m[0]), cross(m[0], m[1]));
}

void main()
{
	int inst_id = gl_InstanceIndex - gl_BaseInstance;
	MeshInfo mesh_info = u_meshes[gl_BaseInstance];
	MeshVertex vert = read_vertex(mesh_info, gl_VertexIndex);
	
	mat4 T = u_transforms[pc.inst.data[inst_id].transform_id];
	mat3 cof = cofactor_signed(mat3(T));

	vec3 pos = (T * vec4(vert.pos, 1)).xyz; 
	vec3 normal = normalize(cof * vert.normal); 

	out_pos = pos;
	out_uv = vert.uv;
	out_normal = normal;
	out_tex_id = pc.inst.data[inst_id].tex_id;
	out_inst_id = inst_id;

	gl_Position = u_pass.pv*vec4(pos, 1);
}

