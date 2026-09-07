#ifndef SORT_ONEPASS_GLSL
#define SORT_ONEPASS_GLSL

#include "sort_defs.glsl"

layout (local_size_x = TILE_SIZE, local_size_y = 1, local_size_z = 1) in;

shared uint tile_id;
shared uint tile_counts[DIGITS];

void sort_execute()
{
	if (gl_LocalInvocationID.x == 0)
		tile_id = atomicAdd(s_next_tile, 1u);

	barrier();

	const uint idx = gl_GlobalInvocationID.x;

	SORT_DATA_TYPE data = s_data[pc.sort.stage & 0x1].buf[idx];

	uint digit = get_digit(data, pc.sort.stage);

	uvec4 mask = digit_mask(digit);
	uint my_count = subgroupBallotBitCount(mask);

	// only the first invocation with a particular digit writes to shared memory.
	//
	// subgroupBallotFindLSB is always defined because at the minimum, this 
	// invocation has this digit.
	const bool is_leader = gl_SubgroupInvocationID == subgroupBallotFindLSB(mask);

	if (is_leader) {
		atomicAdd(tile_counts[digit], my_count);
	}

	barrier();

	if (gl_LocalInvocationID.x < DIGITS) {
		uint my_digit = gl_LocalInvocationID.x;

		uint count = tile_counts[my_digit];
		uint info_slot = tile_id * DIGITS + my_digit;
		atomicExchange(s_tile_info[info_slot], AGGREGATE | (count & VALUE_MASK));

		uint excl_count = 0u;
		int look = int(tile_id) - 1;

		while (look >= 0) {
			uint info;
			do {
				info = atomicAdd(s_tile_info[look * DIGITS + my_digit], 0u);
			} while ((info & STATUS_MASK) == NOT_READY);

			excl_count += info & VALUE_MASK;

			if ((info & STATUS_MASK) == AGGREGATE) {
				--look;
			} else {
				break;
			}
		}

		uint incl_count = excl_count + count;
		atomicExchange(s_tile_info[info_slot], PREFIX | (incl_count));

		tile_counts[my_digit] = 0;
	}

	uint my_offset = subgroupBallotExclusiveBitCount(mask);

	for (int grp = 0; grp < gl_NumSubgroups; ++grp) {
		barrier();

		if (gl_SubgroupID == grp) {
			my_offset += tile_counts[digit];
			if (is_leader)
				tile_counts[digit] += my_count;
		}
	}

	uint out_idx = s_digit_offsets[pc.sort.stage * DIGITS + digit] + my_offset;

	s_data[(pc.sort.stage + 1) & 0x1].buf[out_idx] = data;
}

#endif
