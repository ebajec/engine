#ifndef EV2_GPU_SORT_H
#define EV2_GPU_SORT_H

#include <cstddef>
#include <cstdint>

#include "ev2/pipeline.h"
#include "ev2/resource.h"

struct GPUSort
{
	static constexpr uint32_t TILE_SIZE = 1024; // work group size
	static constexpr uint32_t BITS_PER_DIGIT = 6;
	static constexpr uint32_t DIGITS = (1 << BITS_PER_DIGIT);

	static constexpr uint32_t CLEAR_GROUP_SIZE = 128;
	static constexpr uint32_t CLEAR_OFFSETS_BIT = 0x1;
	static constexpr uint32_t CLEAR_TILE_INFO_BIT = 0x2;

	struct PushConstantHeader
	{
		uint32_t stage;
		uint32_t count;
		uint32_t num_stages;
	};

	size_t max_elem_count;
	uint32_t max_num_passes;

	// DigitOffsets... TileInfo...
	ev2::BufferID buffer;

	ev2::ComputePipelineID p_counting;
	ev2::ComputePipelineID p_sorting;
	ev2::ComputePipelineID p_clear;

	ev2::BindingsID bindings;
	ev2::BindingsID clear_bindings;

	ev2::GfxContext *ctx;

//------------------------------------------------------------------------------

	int init(
		ev2::GfxContext *in_ctx,
		size_t in_elem_count, 
		uint32_t in_pass_count, 
		ev2::ComputePipelineID in_p_counting,
		ev2::ComputePipelineID in_p_sorting
	); 

	void record(
		ev2::PassID pass, 
		uint32_t num_stages, 
		ev2::BufferID data, 
		uint32_t elem_count, 
		size_t elem_size,
		void *pc, 
		size_t pc_size
	);

	~GPUSort();

	constexpr uint32_t get_max_num_offsets()
	{
		return max_num_passes * DIGITS;
	}
	constexpr uint32_t get_max_num_tiles()
	{ 
		return 1 + (max_elem_count - 1)/TILE_SIZE;
	}

	constexpr size_t get_tile_info_bufsize() {
		return (1 + get_max_num_tiles() * sizeof(uint32_t));
	}

	constexpr size_t get_offsets_bufsize()
	{
		return (1 + get_max_num_offsets()) * sizeof(uint32_t);
	}
};

#endif // EV2_GPU_SORT_H

