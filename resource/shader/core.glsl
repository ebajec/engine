#ifndef EV2_CORE_GLSL
#define EV2_CORE_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require

#define BINDLESS_SET 0 
#define PER_FRAME_SET 1
#define PER_PASS_SET 2

#define EV2_BASE_USER_SET 3 
#define EV2_DRAW_INDEX gl_BaseInstance

const uint VERTEX_FORMAT_PN32F = 0;
const uint VERTEX_FORMAT_PNUV32F = 1;
const uint VERTEX_FORMAT_PN16 = 2;
const uint VERTEX_FORMAT_PNUV16 = 3;

const uint VERTEX_SIZE_PN32F = 6;
const uint VERTEX_SIZE_PNUV32F = 8;
const uint VERTEX_SIZE_PN16 = 3;
const uint VERTEX_SIZE_PNUV16 = 4;

layout (buffer_reference, std430, buffer_reference_align = 16) readonly buffer MeshBuffer
{
	vec2 uv_offset, uv_scale;
	uint vtx_count;
	uint vtx_format;
	uint data[];
};

struct MeshInfo
{
	vec3 pos_extent;
	uint first_index;
	vec3 pos_offset;
	uint index_count;
	MeshBuffer buf;
};

struct FrameInfo
{
	uint t_sec;
	float t_fract;
	float t;
	float dt;
	ivec2 resolution;
};

struct PassInfo
{
	mat4 p;
	mat4 v;
	mat4 pv;
	vec3 center;

	// Dimensions of viewport the pass renders into, in pixels. 
	vec2 resolution;
	vec2 inv_resolution;

	float near_plane;
	float far_plane;
};

//------------------------------------------------------------------------------
// Built-in descriptor sets

layout (set = BINDLESS_SET, binding = 0) uniform sampler2D u_textures[];

layout (set = PER_FRAME_SET, std140, binding = 0) uniform FrameInfoBuffer
{
	FrameInfo u_frame;
};

layout (set = PER_FRAME_SET, std140, binding = 1) uniform MeshInfoBuffer
{
	MeshInfo u_meshes[];
};

layout (set = PER_FRAME_SET, std140, binding = 2) uniform TransformBuffer
{
	mat4 u_transforms[];
};

layout (set = PER_PASS_SET, std140, binding = 0) uniform PassInfoBuffer
{
	PassInfo u_pass;
};

//------------------------------------------------------------------------------
// Helpers

float ftime()
{
	return float(u_frame.t_sec) + u_frame.t_fract;
}

vec3 view_dir()
{
	mat4 m = transpose(u_pass.v);
	return -m[2].xyz;
}

struct MeshVertex {
	vec3 pos;
	vec3 normal;
	vec2 uv;
};

MeshVertex read_vertex(MeshInfo mesh, uint vtx)
{
	uint start = 0;
	switch(mesh.buf.vtx_format) {
		case VERTEX_FORMAT_PN16: {
			start = vtx * VERTEX_SIZE_PN16;
			return MeshVertex(vec3(0), vec3(0), vec2(0));
		}
		case VERTEX_FORMAT_PN32F: {
			start = vtx * VERTEX_SIZE_PN32F;
			return MeshVertex(vec3(0), vec3(0), vec2(0));
		}
		case VERTEX_FORMAT_PNUV16: {
			start = vtx * VERTEX_SIZE_PNUV16;

			vec2 px_py = unpackUnorm2x16(mesh.buf.data[start]);
			vec2 pz_nx = unpackUnorm2x16(mesh.buf.data[start + 1]);
			vec2 ny_nz = unpackSnorm2x16(mesh.buf.data[start + 2]);
			vec2 uv = unpackUnorm2x16(mesh.buf.data[start + 3]);

			// normals are snorm
			pz_nx.y -= float(pz_nx.y > 0.5);
			return MeshVertex(
				vec3(px_py.x, px_py.y, pz_nx.x) * mesh.pos_extent + mesh.pos_offset,
				vec3(pz_nx.y, ny_nz),
				vec2(uv * mesh.buf.uv_scale + mesh.buf.uv_offset)
			);
		}
		case VERTEX_FORMAT_PNUV32F: {
			start = vtx * VERTEX_SIZE_PNUV32F;
			vec3 pos = uintBitsToFloat(uvec3(
				mesh.buf.data[start],
				mesh.buf.data[start + 1],
				mesh.buf.data[start + 2]
			));
			vec3 normal = uintBitsToFloat(uvec3(
				mesh.buf.data[start + 3],
				mesh.buf.data[start + 4],
				mesh.buf.data[start + 5]
			));
			vec2 uv = uintBitsToFloat(uvec2(
				mesh.buf.data[start + 6],
				mesh.buf.data[start + 7]
			));
			return MeshVertex(
				pos * mesh.pos_extent + mesh.pos_offset,
				normal,
				uv * mesh.buf.uv_scale + mesh.buf.uv_offset
			);
		}
	}
}

#endif // EV2_CORE_GLSL
