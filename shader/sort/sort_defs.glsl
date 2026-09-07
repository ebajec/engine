#ifndef SORT_DEFS_GLSL
#define SORT_DEFS_GLSL

#extension GL_GOOGLE_include_directive: require
#extension GL_KHR_shader_subgroup_basic: require
#extension GL_KHR_shader_subgroup_ballot: require
#extension GL_KHR_shader_subgroup_arithmetic: require

#define TILE_SIZE 1024

const uint NOT_READY = (0x0 << 30);
const uint AGGREGATE = (0x1 << 30);
const uint PREFIX = (0x2 << 30);
const uint STATUS_MASK = (0x3 << 30);
const uint VALUE_MASK = ~STATUS_MASK;

const uint BITS_PER_DIGIT = 6;
const uint DIGITS = (1 << BITS_PER_DIGIT);

const uint CLEAR_OFFSETS = 0;
const uint CLEAR_TILE_INFO = 1;

struct SortPushConstantHeader
{
	uint stage;
	uint count;
	uint num_stages;
	uint clear_mode;
};
#endif //SORT_DEFS_GLSL
