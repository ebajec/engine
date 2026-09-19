// LSB-first radix sort over `SortSource.Element`, split into a counting pass
// (`sort_count`) and one decoupled-lookback scatter pass per digit
// (`sort_scatter`).
//
// The kernels are generic over an `ISortSource`. See
// resource/shader/sort/sort_uint.slang.
module sort;

public static const uint TILE_SIZE = 1024;
public static const uint CLEAR_GROUP_SIZE = 128;

public static const uint BITS_PER_DIGIT = 6;
public static const uint DIGITS = 1u << BITS_PER_DIGIT;

public static const uint NOT_READY = 0u << 30;
public static const uint AGGREGATE = 1u << 30;
public static const uint PREFIX = 2u << 30;
public static const uint STATUS_MASK = 3u << 30;
public static const uint VALUE_MASK = ~STATUS_MASK;

public static const uint CLEAR_OFFSETS = 0x1;
public static const uint CLEAR_TILE_INFO = 0x2;

public struct TSortPushConstantHeader<T>
{
	public uint count;
	public uint num_stages;
	public uint stage;
	public Ptr<T, Access::Read, AddressSpace::Device> in_data;
	public Ptr<T, Access::ReadWrite, AddressSpace::Device> out_data;
}

public interface ISortSource
{
	associatedtype Element;

	static uint getKey(Element elem);
	static TSortPushConstantHeader<Element> header();
}

// TileInfo[0] is the tile allocation counter; TileInfo[1..] is a
// tile-major DIGITS-wide table of decoupled-lookback status words.
[vk::binding(0, 0)]
RWStructuredBuffer<uint> TileInfo;

// DigitOffsets[0] is the counting-pass completion counter; DigitOffsets[1..]
// holds num_stages * DIGITS global digit offsets.
[vk::binding(1, 0)]
RWStructuredBuffer<uint> DigitOffsets;

//------------------------------------------------------------------------------
// ballot helpers

uint ballot_bit_count(uint4 mask)
{
	return countbits(mask.x) + countbits(mask.y) +
		countbits(mask.z) + countbits(mask.w);
}

uint ballot_find_lsb(uint4 mask)
{
	[ForceUnroll]
	for (uint i = 0; i < 4; ++i) {
		if (mask[i] != 0)
			return 32 * i + firstbitlow(mask[i]);
	}
	return ~0u;
}

// bits [0, lane) set
uint4 lane_mask_lt(uint lane)
{
	uint4 mask = uint4(0);
	[ForceUnroll]
	for (uint i = 0; i < 4; ++i) {
		uint lo = 32 * i;
		mask[i] = lane <= lo ? 0u
			: (lane >= lo + 32 ? ~0u : (1u << (lane - lo)) - 1u);
	}
	return mask;
}

// one for each active invocation in this wave whose digit matches `digit`
uint4 digit_mask(uint digit)
{
	uint4 mask = uint4(~0u);

	[ForceUnroll]
	for (uint b = 0; b < BITS_PER_DIGIT; ++b) {
		bool is_set = ((digit >> b) & 0x1) != 0;
		// one for all invocations whose digit has this bit set
		uint4 ballot = WaveActiveBallot(is_set);
		mask &= is_set ? ballot : ~ballot;
	}

	return mask;
}

uint digit_of(uint key, uint stage)
{
	return (key >> (stage * BITS_PER_DIGIT)) & (DIGITS - 1);
}

uint num_tiles_for(uint count)
{
	return (count + TILE_SIZE - 1) / TILE_SIZE;
}

//------------------------------------------------------------------------------

public void sort_clear_offsets<SortSource : ISortSource>(uint3 dispatch_id)
{
	const TSortPushConstantHeader<SortSource.Element> hdr = SortSource.header();
	const uint idx = dispatch_id.x;

	if (idx == 0)
		DigitOffsets[0] = 0;
	if (idx < hdr.num_stages * DIGITS)
		DigitOffsets[1 + idx] = 0;
}

//------------------------------------------------------------------------------

public void sort_clear_tile_info<SortSource : ISortSource>(uint3 dispatch_id)
{
	const TSortPushConstantHeader<SortSource.Element> hdr = SortSource.header();
	const uint idx = dispatch_id.x;

	if (idx == 0)
		TileInfo[0] = 0;
	if (idx < num_tiles_for(hdr.count) * DIGITS)
		TileInfo[1 + idx] = NOT_READY;
}
//------------------------------------------------------------------------------

groupshared uint g_completion_idx;
groupshared uint g_counts[DIGITS];

public void sort_count<SortSource : ISortSource>(uint3 dispatch_id, uint group_idx)
{
	const TSortPushConstantHeader<SortSource.Element> hdr = SortSource.header();

	const uint idx = dispatch_id.x;
	const bool is_valid = idx < hdr.count;
	const uint4 valid_mask = WaveActiveBallot(is_valid);

	const uint lane = WaveGetLaneIndex();

	SortSource.Element data = hdr.in_data[is_valid ? idx : 0];
	uint key = SortSource.getKey(data);

	for (uint i = 0; i < hdr.num_stages; ++i) {
		if (group_idx < DIGITS)
			g_counts[group_idx] = 0;

		GroupMemoryBarrierWithGroupSync();

		uint digit = digit_of(key, i);
		uint4 mask = digit_mask(digit) & valid_mask;

		if (is_valid && lane == ballot_find_lsb(mask))
			InterlockedAdd(g_counts[digit], ballot_bit_count(mask));

		GroupMemoryBarrierWithGroupSync();

		if (group_idx < DIGITS) {
			uint prev;
			InterlockedAdd(
				DigitOffsets[1 + i * DIGITS + group_idx],
				g_counts[group_idx],
				prev
			);
		}

		// keep the next iteration's zeroing from racing the read above
		GroupMemoryBarrierWithGroupSync();
	}

	if (group_idx == 0)
		InterlockedAdd(DigitOffsets[0], 1u, g_completion_idx);

	GroupMemoryBarrierWithGroupSync();

	// the last workgroup to finish owns the scan
	if (g_completion_idx < num_tiles_for(hdr.count) - 1 || group_idx > 0)
		return;

	for (uint i = 0; i < hdr.num_stages; ++i) {
		uint running = 0;
		for (uint digit = 0; digit < DIGITS; ++digit) {
			uint slot = 1 + i * DIGITS + digit;
			uint digit_count = DigitOffsets[slot];
			DigitOffsets[slot] = running;
			running += digit_count;
		}
	}
}

//------------------------------------------------------------------------------

groupshared uint g_tile_id;
groupshared uint g_tile_counts[DIGITS];

public void sort_scatter<SortSource : ISortSource>(uint3 dispatch_id, uint group_idx)
{
	const TSortPushConstantHeader<SortSource.Element> hdr = SortSource.header();

	if (group_idx == 0)
		InterlockedAdd(TileInfo[0], 1u, g_tile_id);

	if (group_idx < DIGITS)
		g_tile_counts[group_idx] = 0;

	GroupMemoryBarrierWithGroupSync();

	const uint idx = group_idx + TILE_SIZE * g_tile_id;
	const bool is_valid = idx < hdr.count;
	const uint4 valid_mask = WaveActiveBallot(is_valid);

	const uint lane = WaveGetLaneIndex();
	const uint lane_count = WaveGetLaneCount();
	const uint wave_id = group_idx / lane_count;
	const uint num_waves = 1 + (TILE_SIZE - 1) / lane_count;

	SortSource.Element data = hdr.in_data[is_valid ? idx : 0];
	uint digit = digit_of(SortSource.getKey(data), hdr.stage);

	uint4 mask = digit_mask(digit) & valid_mask;
	uint my_count = ballot_bit_count(mask);

	// only the first invocation with a particular digit writes to shared memory
	const bool is_leader = is_valid && lane == ballot_find_lsb(mask);

	if (is_leader)
		InterlockedAdd(g_tile_counts[digit], my_count);

	GroupMemoryBarrierWithGroupSync();

	if (group_idx < DIGITS) {
		uint my_digit = group_idx;

		uint tile_count = g_tile_counts[my_digit];
		uint info_slot = 1 + g_tile_id * DIGITS + my_digit;

		uint prev;
		InterlockedExchange(
			TileInfo[info_slot],
			AGGREGATE | (tile_count & VALUE_MASK),
			prev
		);

		uint excl_count = 0;
		int look = int(g_tile_id) - 1;

		while (look >= 0) {
			uint info;
			do {
				InterlockedAdd(TileInfo[1 + uint(look) * DIGITS + my_digit], 0u, info);
			} while ((info & STATUS_MASK) == NOT_READY);

			excl_count += info & VALUE_MASK;

			if ((info & STATUS_MASK) == AGGREGATE)
				--look;
			else
				break;
		}

		uint incl_count = excl_count + tile_count;
		InterlockedExchange(
			TileInfo[info_slot],
			PREFIX | (incl_count & VALUE_MASK),
			prev
		);

		g_tile_counts[my_digit] = excl_count;
	}

	uint my_offset = ballot_bit_count(mask & lane_mask_lt(lane));

	for (uint grp = 0; grp < num_waves; ++grp) {
		GroupMemoryBarrierWithGroupSync();

		if (wave_id == grp) {
			my_offset += g_tile_counts[digit];
			if (is_leader)
				g_tile_counts[digit] += my_count;
		}
	}

	if (is_valid) {
		uint out_idx = DigitOffsets[1 + hdr.stage * DIGITS + digit] + my_offset;
		hdr.out_data[out_idx] = data;
	}
}
