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

const uint MAX_TILE_PAGES = 16;
const uint TILE_VERT_WIDTH = 32;
const uint TILE_VERT_COUNT = TILE_VERT_WIDTH*TILE_VERT_WIDTH;

layout (binding = 0) uniform sampler2D u_tex;
layout (binding = 1) uniform sampler2DArray u_tex_arrays[MAX_TILE_PAGES];

layout (std430, binding = 0) buffer TexIndices
{
	uint tex_indices[];
};

tex_idx_t decode_tex_idx(uint idx)
{
	tex_idx_t tex_idx;

	tex_idx.page = idx & 0x0000FFFF;
	tex_idx.tex = (idx & 0xFFFF0000) >> 16;

	return tex_idx;
}

#endif // GLOBE_GLSL

