#include "gpu_sort.h"

#include <cassert>

int GPUSort::init(
	ev2::GfxContext *in_ctx,
	size_t in_elem_count, 
	uint32_t in_pass_count, 
	ev2::ComputePipelineID in_p_counting,
	ev2::ComputePipelineID in_p_sorting
)
{
	ctx = in_ctx;

	p_counting = in_p_counting;
	p_sorting = in_p_sorting;

	max_elem_count = in_elem_count;
	max_num_passes = in_pass_count; 

	size_t bufsize = get_tile_info_bufsize() + get_offsets_bufsize();

	buffer = ev2::create_buffer(ctx, bufsize, 
		ev2::BUFFER_USAGE_STORAGE_BUFFER_BIT); 

	bindings = ev2::create_bindings(in_ctx, p_counting, 0, ev2::BINDING_MODE_DYNAMIC);

	return 0;
}

GPUSort::~GPUSort()
{
	if (buffer.is_valid()) {
		ev2::destroy_buffer(ctx, buffer);
	}
}

void GPUSort::record(
	ev2::PassID pass, 
	uint32_t num_stages, 
	ev2::BufferID data, 
	uint32_t elem_count, 
	size_t elem_size,
	void *pc, 
	size_t pc_size
)
{
	const char *metadata_names[2] = {
		"TileInfo",
		"DigitOffsets",
	};
	const size_t metadata_sizes[2] = {
		get_tile_info_bufsize(),
		get_offsets_bufsize()
	};

	ev2::reset_bindings(ctx, bindings);
	size_t offset = 0;
	for (int i = 0; i < 2; ++i) {
		size_t size = metadata_sizes[i];
		ev2::bind_buffer(ctx, bindings, metadata_names[i], buffer, offset, size); 
		offset += size;
	}

	size_t data_size = elem_count * elem_size;;
	size_t data_offset = 0;
	for (int i = 0; i < 2; ++i) {
		ev2::bind_buffer_indexed(ctx, bindings, "Data", i, data, data_offset, data_size);
		data_offset += data_size;
	}
	ev2::flush_bindings(ctx, bindings);

	assert(elem_size <= max_elem_count);

	const uint32_t num_tiles = 1 + (elem_count - 1) / TILE_SIZE;
	const uint32_t num_offsets = num_stages * DIGITS;

	PushConstantHeader header = {
		.stage = 0,
		.count = (uint32_t)elem_count,
		.num_stages = num_stages,
	};

	struct {
		uint32_t count;
		uint32_t flags;
	} clear_pc;

	// clear the counting buffer
	ev2::cmd_use_buffer(pass, buffer, ev2::USAGE_STORAGE_WRITE_COMPUTE);
	ev2::cmd_bind_resources(pass, clear_bindings);
	ev2::cmd_push_constant(pass, p_clear, 0, sizeof(clear_pc), &clear_pc);
	ev2::cmd_bind_compute_pipeline(pass, p_clear);
	ev2::cmd_dispatch(pass, (num_offsets - 1)/CLEAR_GROUP_SIZE, 1, 1);

	// counting
	ev2::cmd_bind_resources(pass, bindings);

	ev2::cmd_push_constant(pass, p_counting, 0, sizeof(header), &header);
	ev2::cmd_push_constant(pass, p_counting, sizeof(header), pc_size, pc);

	ev2::cmd_use_buffer(pass, buffer, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
	ev2::cmd_use_buffer(pass, data, ev2::USAGE_STORAGE_READ_COMPUTE);

	ev2::cmd_bind_compute_pipeline(pass, p_counting);
	ev2::cmd_dispatch(pass, num_tiles, 1, 1);

	for (int i = 0; i < header.num_stages; ++i) {
		// clear the tile_info
		ev2::cmd_use_buffer(pass, buffer, ev2::USAGE_STORAGE_WRITE_COMPUTE);
		ev2::cmd_bind_resources(pass, clear_bindings);
		ev2::cmd_push_constant(pass, p_clear, 0, sizeof(clear_pc), &clear_pc);
		ev2::cmd_bind_compute_pipeline(pass, p_clear);
		ev2::cmd_dispatch(pass, (num_offsets - 1)/CLEAR_GROUP_SIZE, 1, 1);

		ev2::cmd_use_buffer(pass, buffer, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
		ev2::cmd_use_buffer(pass, data, ev2::USAGE_STORAGE_READ_COMPUTE);

		++header.stage;
		ev2::cmd_push_constant(pass, p_counting, 
			offsetof(PushConstantHeader,stage), 
			sizeof(PushConstantHeader::stage), 
			&header.stage
		);
	}

}

