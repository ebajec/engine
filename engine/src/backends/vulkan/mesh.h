#ifndef EV2_MESH_H
#define EV2_MESH_H

#include <ev2/resource.h>
#include <ev2/asset.h>

namespace ev2 {

struct GfxContext;

enum IndexType
{
	INDEX_TYPE_UINT32,
	INDEX_TYPE_UINT16,
	INDEX_TYPE_UINT8,
};

enum VertexType
{
	VERTEX_TYPE_CUSTOM,
	VERTEX_TYPE_PN_32F,
	VERTEX_TYPE_PN_16F,
	VERTEX_TYPE_PNUV_32F,
	VERTEX_TYPE_PNUV_16F,
};

struct VertexPNUV32f {
	float position[3];
	float normal[3];
	float uv[2];
};

struct VertexPN32f {
	float position[3];
	float normal[3];
};

struct VertexPNUV16f {
	uint16_t position[3];
	uint16_t normal[3];
	uint16_t uv[2];
};

struct VertexPN16f {
	uint16_t position[3];
	uint16_t normal[3];
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wswitch-enum"
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
#pragma clang diagnostic pop

constexpr uint32_t get_vtx_type_size(VertexType type)
{
	switch (type) {
		case VERTEX_TYPE_PN_32F: return sizeof(VertexPN32f);
		case VERTEX_TYPE_PN_16F: return sizeof(VertexPN16f);
		case VERTEX_TYPE_PNUV_32F: return sizeof(VertexPNUV32f);
		case VERTEX_TYPE_PNUV_16F: return sizeof(VertexPNUV16f);
		case VERTEX_TYPE_CUSTOM: return 0;
	}
}

constexpr uint32_t get_vtx_type_align(VertexType type)
{
	switch (type) {
		case VERTEX_TYPE_PN_32F: return alignof(VertexPN32f);
		case VERTEX_TYPE_PN_16F: return alignof(VertexPN16f);
		case VERTEX_TYPE_PNUV_32F: return alignof(VertexPNUV32f);
		case VERTEX_TYPE_PNUV_16F: return alignof(VertexPNUV16f);
		case VERTEX_TYPE_CUSTOM: return 0;
	}
}

struct Mesh {
	IndexType idx_type;

	VertexType 	vtx_type;
	uint32_t 	vtx_size;
	uint32_t 	vtx_align;

	uint32_t idx_count;
	uint32_t vtx_count;

	void *idx_data;
	void *vtx_data;

	uint32_t ibo_offset;
	uint32_t vbo_offset;

	ev2::BufferID ibo;
	ev2::BufferID vbo;

	constexpr size_t vbo_size_bytes() {
		return (size_t)vtx_count * (size_t)vtx_size;}
	constexpr size_t ibo_size_bytes() {
		return (size_t)idx_count * (size_t)get_idx_type_size(idx_type);}
};

struct MeshParams {
	IndexType idx_type;
	VertexType 	vtx_type;

	uint32_t 	vtx_size;
	uint32_t 	vtx_align;

	uint32_t idx_count;
	uint32_t vtx_count;

	void *idx_data;
	void *vtx_data;
};

extern Mesh *create_mesh_internal(GfxContext *ctx, const MeshParams *params);
extern void destroy_mesh_internal(GfxContext *ctx);

extern Result ensure_mesh_buffers_loaded(GfxContext *ctx, MeshID mesh);

}

#endif



