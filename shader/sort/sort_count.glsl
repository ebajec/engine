#ifndef SORT_COUNT_GLSL
#define SORT_COUNT_GLSL

#include "sort_defs.glsl"

layout (local_size_x = TILE_SIZE, local_size_y = 1, local_size_z = 1) in;

shared uint completion_idx;
shared uint counts[DIGITS];

void sort_count_execute()
{
	const uint idx = gl_GlobalInvocationID.x;

	SORT_DATA_TYPE data = s_data[pc.sort.stage & 0x1].buf[idx];

	uint key = sort_get_key(data);

	for (int i = 0; i < pc.sort.num_stages; ++i) {
		if (gl_SubgroupID == 0)
			counts[gl_SubgroupInvocationID & (DIGITS - 1)] = 0;

		barrier();

		uint digit = (key >> (i * BITS_PER_DIGIT)) & (DIGITS - 1);
		uvec4 mask = digit_mask(digit);

		if (gl_SubgroupInvocationID == subgroupBallotFindLSB(mask))
			atomicAdd(counts[digit], subgroupBallotBitCount(mask));

		barrier();

		if (gl_LocalInvocationID.x < DIGITS) {
			uint my_digit = gl_LocalInvocationID.x;
			atomicAdd(s_digit_offsets[i * DIGITS + my_digit], counts[my_digit]);
		}
	}

	if (gl_LocalInvocationID.x == 0)
		completion_idx = atomicAdd(s_completion_counter, 1u);

	barrier();

	if (completion_idx < gl_NumWorkGroups.x - 1 || gl_SubgroupID > 0)
		return;

	for (uint i = 0; i < pc.sort.num_stages; ++i) {
		uint my_digit = gl_SubgroupInvocationID;

		const bool is_valid = my_digit < DIGITS;

		uint digit_count = is_valid ? s_digit_offsets[my_digit] : 0;
		uint offset = subgroupExclusiveAdd(digit_count);

		if (is_valid) {
			s_digit_offsets[i * DIGITS + my_digit] = offset;
		}
	}
}

#endif // SORT_COUNT_GLSL
