module sort_uint;

import sort;

[vk::push_constant]
ConstantBuffer<TSortPushConstantHeader<uint32_t>> pc;

uint spread2(uint x)
{
	x &= 0xFFFF;
	x = (x | (x << 8)) & 0x00FF00FF;
	x = (x | (x << 4)) & 0x0F0F0F0F;
	x = (x | (x << 2)) & 0x33333333;
	x = (x | (x << 1)) & 0x55555555;
	return x;
}

uint morton2(uint x, uint y)
{
	return spread2(x) | (spread2(y) << 1);
}

public struct IntegerSortSource : ISortSource
{
	public typealias Element = uint32_t;

	public static uint getKey(uint key)
	{
		return key;
	}

	public static TSortPushConstantHeader<Element> header()
	{
		return pc;
	}
}

typealias S = IntegerSortSource;

[shader("compute")][numthreads(CLEAR_GROUP_SIZE, 1, 1)]
void clear_offsets(uint3 d: SV_DispatchThreadID) { sort_clear_offsets<S>(d); }

[shader("compute")][numthreads(CLEAR_GROUP_SIZE, 1, 1)]
void clear_tile_info(uint3 d: SV_DispatchThreadID) { sort_clear_tile_info<S>(d); }

[shader("compute")][numthreads(TILE_SIZE, 1, 1)]
void count(uint3 d: SV_DispatchThreadID, uint g: SV_GroupIndex) { sort_count<S>(d, g); }

[shader("compute")][numthreads(TILE_SIZE, 1, 1)]
void scatter(uint3 d: SV_DispatchThreadID, uint g: SV_GroupIndex) { sort_scatter<S>(d, g); }
