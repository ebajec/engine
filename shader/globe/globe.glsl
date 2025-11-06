#ifndef GLOBE_GLSL
#define GLOBE_GLSL

struct aabb2_t
{
	vec2 min, max;
};

struct tile_code_t
{
	uint face;
	uint zoom; 
	uint idx;
};

struct tex_idx_t
{
	uint page;
	uint tex;
};

struct metadata_t
{
	vec4 coord;

	vec2 tex_uv[2];
	vec2 globe_uv[2];

	uint tex_idx;
	uint code_lower;
	uint code_upper;
};

const uint MAX_TILE_PAGES = 16;
const uint TILE_VERT_WIDTH = 64;
const uint TILE_VERT_COUNT = TILE_VERT_WIDTH*TILE_VERT_WIDTH;

layout (binding = 0) uniform sampler2D u_tex;
layout (binding = 1) uniform sampler2DArray u_tex_arrays[MAX_TILE_PAGES];

layout (std430, binding = 0) buffer Metadata
{
	metadata_t metadata[];
};

tex_idx_t decode_tex_idx(uint idx)
{
	tex_idx_t tex_idx;

	tex_idx.page = idx & 0x0000FFFF;
	tex_idx.tex = (idx & 0xFFFF0000) >> 16;

	return tex_idx;
}

bool is_valid(tex_idx_t idx)
{
	return !(idx.page == 0xFFFF && idx.tex == 0xFFFF);
}

#endif // GLOBE_GLSL

