#ifndef SORT_LAYOUT_GLSL
#define SORT_LAYOUT_GLSL

// Example usage:
// layout (push_constant) uniform PC 
// {
// 		PushConstantHeader hdr;
//		...
// };
//
// #define SORT_DATA_TYPE uint
// #define sort_get_key(SORT_DATA_TYPE x)
// {
// 	return x;
// }

layout (std430, set = 0, binding = 0) buffer TileInfo {
	uint s_next_tile;
	uint s_tile_info[];
};

layout (std430, set = 0, binding = 1) buffer DigitOffsets {
	uint s_completion_counter;
	uint s_digit_offsets[];
};

layout (std430, set = 0, binding = 2) buffer Data
{
	SORT_DATA_TYPE buf[];
} s_data[2];

uint get_digit(SORT_DATA_TYPE data, uint stage)
{
	uint key = sort_get_key(data);
	return (key >> (pc.sort.stage * BITS_PER_DIGIT)) & (DIGITS - 1); 
}

// this will be set to 1 for each invocation whose digit matches the passed digit
uvec4 digit_mask(uint digit)
{
	uvec4 mask = uvec4(0xFFFFFFFF);

	// reduce mask to only be one for invocations whose digit agrees
	// with our designated one
	for (int b = 0; b < BITS_PER_DIGIT; ++b) {
		bool is_set = bool((digit >> b) & 0x1);
		// one for all invocations whose digit has the this bit set
		uvec4 ballot = subgroupBallot(is_set); 
		mask &= is_set ? ballot : ~ballot;
	}

	return mask;
}
#endif // SORT_LAYOUT_GLSL
