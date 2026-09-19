module linear_bvh;

public struct AABB<let N : int>
{
	public float min[N];
	public float max[N];
};

public struct BVH<let N : int>
{
	public uint32_t elem_count;
	public uint32_t internal_size;
	uint32_t padding[2];
	public AABB<N> boxes[];
};

public struct LinearBVHHeader<Element, let N : int>
{
	public uint32_t in_start;
	public uint32_t in_count;
	public uint32_t elem_count;
	public Ptr<BVH<N>, Access::ReadWrite, AddressSpace::Device, Std430DataLayout> bvh;
	public Ptr<Element, Access::Read, AddressSpace::Device, Std430DataLayout> data;
};

public interface ILinearBVH
{
	public static const uint N;
	public static const uint GroupSize;
	public static const uint InternalSize;

	associatedtype Element;

	public static LinearBVHHeader<Element, N> header();
	public static AABB<N> bounds(Element);
}

static const float FLOAT_MAX = 3.402823466e+38;

static AABB<N> empty_box<let N : int>()
{
	AABB<N> box;
	[ForceUnroll]
	for (uint i = 0; i < N; ++i) {
		box.min[i] = FLOAT_MAX;  
		box.max[i] = -FLOAT_MAX;  
	}
	return box;
}

struct Range 
{
	uint start;
	uint count;
};

Range range_for(uint level, uint count, uint internal_size)
{	
	const uint log_size = firstbithigh(internal_size);

	uint total = 0;
	uint tmp_count = count;
	do {
		tmp_count = (tmp_count + log_size - 1) >> log_size;
		total += tmp_count;
	} while (tmp_count > 1);

	uint level_count = count;
	for (uint i = 0; i < level; ++i) {
		level_count = (level_count + log_size - 1) >> log_size; 
		total -= level_count;
	}
	return Range(total, level_count);
}

public void linear_bvh_compute_level<LinearBVH : ILinearBVH>(uint3 dispatch_id, uint group_idx)
{
	static const uint N = LinearBVH.N;
	static const uint InternalSize = LinearBVH.InternalSize;
	static const uint GroupSize = LinearBVH.GroupSize;
	static const uint NumShared = (GroupSize + InternalSize - 1)/InternalSize;

	static groupshared AABB<N> g_box[NumShared];

	LinearBVHHeader<LinearBVH.Element,N> hdr = LinearBVH.header();

	if (dispatch_id.x == 0) {
		hdr.bvh.internal_size = InternalSize;
		hdr.bvh.elem_count = hdr.elem_count;
	}

	const uint box_id = group_idx / InternalSize; 
	const uint box_idx = group_idx % InternalSize; 
	const uint lane_box_idx = WaveGetLaneIndex() % InternalSize; 

	if (box_idx == 0) {
		g_box[box_id] = empty_box<N>();
	}

	GroupMemoryBarrierWithGroupSync();

	//const Range range = range_for(hdr.level, hdr.bvh.elem_count, InternalSize);
	const uint idx = dispatch_id.x;
	const bool is_valid = idx < hdr.in_count;
	const bool is_first_level = hdr.in_count == hdr.elem_count; 

	AABB<N> vol; 

	vol = is_valid ? 
		(is_first_level ? LinearBVH.bounds(hdr.data[idx]) : hdr.bvh.boxes[hdr.in_start + idx]) :
		empty_box<N>();

	[ForceUnroll]
	for (uint i = 0; i < N; ++i) {
		[ForceUnroll]
		for (int j = 0; j < NumShared; ++j) {
 			if (j == box_id) {
				float wave_min = WaveActiveMin(vol.min[i]);
				float wave_max = WaveActiveMax(vol.max[i]);
				if (lane_box_idx == 0) {
					InterlockedMin(g_box[box_id].min[i], wave_min);
					InterlockedMax(g_box[box_id].max[i], wave_max);
				}
			}
		}
	}

	const uint32_t out_count = (hdr.in_count + InternalSize - 1) / InternalSize;
	const uint32_t out_start = is_first_level ? 0 : hdr.in_start + hdr.in_count;
	const uint32_t out_idx = idx / InternalSize;
	
	const bool out_valid = box_idx == 0 && out_idx < out_count;

	GroupMemoryBarrierWithGroupSync();

	if (out_valid)
		hdr.bvh.boxes[out_start + out_idx] = g_box[box_id];
}
