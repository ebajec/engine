module sort_uint;

import "core/shader/sort/sort.slang";

[vk::push_constant]
ConstantBuffer<TSortPushConstantHeader<uint32_t>> pc;

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
