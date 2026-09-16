#include "gpu_sort.h"

#include <cassert>
#include <ev2/utils/common.h>

struct SortPushConstantHeader
{
	uint32_t stage;
	uint32_t count;
	uint32_t num_stages;
};

GPUSort* GPUSort::create(
	ev2::GfxContext *ctx,
	size_t in_elem_count, 
	uint32_t max_num_bits, 
	const char *pipeline)
{
	GPUSort *sorter = new GPUSort;

	sorter->ctx = ctx;

	assert(max_num_bits <= 32);

	sorter->p_counting = ev2::load_compute_pipeline(ctx, pipeline, "count");
	sorter->p_scatter = ev2::load_compute_pipeline(ctx, pipeline, "scatter");
	sorter->p_clear_offsets = ev2::load_compute_pipeline(ctx, pipeline, "clear_offsets");
	sorter->p_clear_tiles = ev2::load_compute_pipeline(ctx, pipeline, "clear_tile_info");

	sorter->max_elem_count = in_elem_count;
	sorter->max_num_passes = 1 + (max_num_bits - 1)/BITS_PER_DIGIT; 

	size_t align = ev2::get_vulkan_physical_device_limits(ctx)->minStorageBufferOffsetAlignment;

	size_t bufsize = align_up(sorter->get_tile_info_bufsize(), align) + align_up(sorter->get_offsets_bufsize(), align);

	sorter->buffer = ev2::create_buffer(ctx, bufsize, 
		ev2::BUFFER_USAGE_STORAGE_BUFFER_BIT); 

	sorter->bindings = ev2::create_bindings(ctx, sorter->p_counting, 0, ev2::BINDING_MODE_STATIC);

	const char *metadata_names[2] = {
		"TileInfo",
		"DigitOffsets",
	};

	const size_t metadata_sizes[2] = {
		align_up(sorter->get_tile_info_bufsize(), align),
		align_up(sorter->get_offsets_bufsize(), align)
	};

	size_t offset = 0;
	for (int i = 0; i < 2; ++i) {
		size_t size = metadata_sizes[i];
		ev2::bind_buffer(ctx, sorter->bindings, metadata_names[i], sorter->buffer, offset, size); 
		offset += size;
	}

	ev2::flush_bindings(ctx, sorter->bindings);

	return sorter;
}

GPUSort::~GPUSort()
{
	if (buffer.is_valid()) {
		ev2::destroy_buffer(ctx, buffer);
	}
	if (bindings.is_valid()) {
		ev2::destroy_bindings(ctx, bindings);
	}
}

ev2::BufferID GPUSort::record(
	ev2::PassID pass, 
	uint32_t num_bits, 
	ev2::BufferID data[2], 
	uint32_t elem_count, 
	size_t elem_size,
	void *pc, 
	size_t pc_size
)
{
	assert(elem_count <= max_elem_count);
	assert(num_bits <= max_num_passes * BITS_PER_DIGIT);

	const uint32_t num_stages = (num_bits + BITS_PER_DIGIT - 1) / BITS_PER_DIGIT; 
	const uint32_t num_tiles = (elem_count + TILE_SIZE - 1) / TILE_SIZE;
	const uint32_t num_offsets = num_stages * DIGITS;

	VkDeviceAddress buf_addrs[2] = {
		ev2::get_buffer_device_address(ctx, data[0]),
		ev2::get_buffer_device_address(ctx, data[1])
	};

	PushConstantHeader header = {
		.count = (uint32_t)elem_count,
		.num_stages = num_stages,
		.stage = 0,
		.in_data = buf_addrs[0]
	};

	ev2::cmd_bind_resources(pass, bindings);
	ev2::cmd_push_constant(pass, p_counting, 0, sizeof(header), &header);

	if (pc && pc_size)
		ev2::cmd_push_constant(pass, p_counting, sizeof(header), pc_size, pc);

	// clear the counting buffer
	ev2::cmd_use_buffer(pass, buffer, ev2::USAGE_STORAGE_WRITE_COMPUTE);

	ev2::cmd_bind_compute_pipeline(pass, p_clear_offsets);
	ev2::cmd_dispatch(pass, (num_offsets + CLEAR_GROUP_SIZE - 1)/CLEAR_GROUP_SIZE, 1, 1);

	ev2::cmd_use_buffer(pass, buffer, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
	ev2::cmd_use_buffer(pass, data[0], ev2::USAGE_STORAGE_READ_COMPUTE);

	ev2::cmd_bind_compute_pipeline(pass, p_counting);
	ev2::cmd_dispatch(pass, num_tiles, 1, 1);

	for (int i = 0; i < num_stages; ++i) {
		// clear the tile_info
		ev2::cmd_use_buffer(pass, buffer, ev2::USAGE_STORAGE_WRITE_COMPUTE);

		ev2::cmd_bind_compute_pipeline(pass, p_clear_tiles);
		ev2::cmd_dispatch(pass, (num_tiles * DIGITS + CLEAR_GROUP_SIZE - 1)/CLEAR_GROUP_SIZE, 1, 1);

		ev2::cmd_use_buffer(pass, buffer, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
		ev2::cmd_use_buffer(pass, data[i & 0x1], ev2::USAGE_STORAGE_READ_COMPUTE);
		ev2::cmd_use_buffer(pass, data[(i + 1) & 0x1], ev2::USAGE_STORAGE_WRITE_COMPUTE);

		header.in_data = buf_addrs[i & 0x1];
		header.out_data = buf_addrs[(i + 1) & 0x1];

		size_t pc_start = offsetof(PushConstantHeader, stage); 
		ev2::cmd_push_constant(pass, p_scatter, pc_start, sizeof(header) - pc_start, &header.stage);

		ev2::cmd_bind_compute_pipeline(pass, p_scatter);
		ev2::cmd_dispatch(pass, num_tiles, 1, 1);

		++header.stage;
	}

	return data[(header.num_stages) & 0x1]; 
}
