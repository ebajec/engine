#ifndef EV2_MESH_H
#define EV2_MESH_H

#include <ev2/core.h>
#include <ev2/utils/common.h>

namespace ev2 {

struct GfxContext;

struct alignas(16) MeshBufferHeaderGPU
{
	float uv_scale[2];
	float uv_offset[2];
	uint32_t count;
	VertexFormat format;
};

struct alignas(16) MeshInfoGPU
{
	float pos_extent[3];
	uint32_t first_index;
	float pos_offset[3];
	uint32_t index_count;
	VkDeviceAddress buffer;
};

constexpr uint32_t get_idx_type_align(IndexType type)
{
	switch (type) {
		case INDEX_TYPE_UINT32: return alignof(uint32_t);
		case INDEX_TYPE_UINT16: return alignof(uint16_t);
		case INDEX_TYPE_UINT8: return alignof(uint8_t);
		default: return 0;
	}
}

constexpr uint32_t get_idx_type_size(IndexType type)
{
	switch (type) {
		case INDEX_TYPE_UINT32: return sizeof(uint32_t);
		case INDEX_TYPE_UINT16: return sizeof(uint16_t);
		case INDEX_TYPE_UINT8: return sizeof(uint8_t);
		default: return 0;
	}
}

constexpr VkIndexType idx_type_to_vk(IndexType type)
{
	switch (type) {
		case INDEX_TYPE_UINT32: return VK_INDEX_TYPE_UINT32;
		case INDEX_TYPE_UINT16: return VK_INDEX_TYPE_UINT16;
		case INDEX_TYPE_UINT8: return VK_INDEX_TYPE_UINT8;
		default: return VK_INDEX_TYPE_UINT32;
	}
}

constexpr uint32_t get_vtx_type_size(VertexFormat type)
{
	switch (type) {
		case VERTEX_FORMAT_PN32F: return sizeof(VertexPN32f);
		case VERTEX_FORMAT_PN16: return sizeof(VertexPN16);
		case VERTEX_FORMAT_PNUV32F: return sizeof(VertexPNUV32f);
		case VERTEX_FORMAT_PNUV16: return sizeof(VertexPNUV16);
		case VERTEX_FORMAT_CUSTOM: return 0;
	}
}

constexpr uint32_t get_vtx_type_align(VertexFormat type)
{
	switch (type) {
		case VERTEX_FORMAT_PN32F: return alignof(VertexPN32f);
		case VERTEX_FORMAT_PN16: return alignof(VertexPN16);
		case VERTEX_FORMAT_PNUV32F: return alignof(VertexPNUV32f);
		case VERTEX_FORMAT_PNUV16: return alignof(VertexPNUV16);
		case VERTEX_FORMAT_CUSTOM: return 0;
	}
}

struct Mesh {
	MeshGeometry geom;

	size_t buffer_offset;
	ev2::BufferID buffer;

	constexpr size_t vbo_size_bytes() {
		return (size_t)geom.vtx_count * geom.vtx_size;}
	constexpr size_t ibo_size_bytes() {
		return (size_t)geom.idx_count * (size_t)get_idx_type_size(geom.idx_type);}

	// @brief compute vbo and ibo starts into a buffer starting at base.
	// @return total buffer size required.
	constexpr size_t prepare_offsets(size_t base, size_t *vbo_start, size_t *ibo_start)
	{
		size_t offset = align_up(base + sizeof(MeshBufferHeaderGPU), 16);
		if (vbo_start)
			*vbo_start = offset;
		offset = align_up(offset + vbo_size_bytes(), 16);
		if (ibo_start)
			*ibo_start = offset;
		offset = align_up(offset + ibo_size_bytes(), 16);
		return offset;
	}
};

extern Result mesh_ensure_loaded_gpu(GfxContext *ctx, MeshID mesh);

}

#endif


