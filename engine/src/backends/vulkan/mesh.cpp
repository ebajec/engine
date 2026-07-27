#include "ev2/asset.h"

#include "backends/vulkan/context.h"
#include "utils/asset_table.h"

#include "mesh.h"

namespace ev2 {


Mesh *create_mesh_internal(GfxContext *ctx, const MeshParams *params)
{
	const bool is_custom = params->vtx_type == VERTEX_TYPE_CUSTOM; 

	if (is_custom && params->vtx_size == 0) {
		log_error( 
			"If vtx_type is VERTEX_TYPE_CUSTOM, vtx_size (%d) and vtx_align (%d) must be nonzero",
			params->vtx_size, params->vtx_align);
		return nullptr;
	}

	if (!is_custom && params->vtx_size != 0) {
		log_error( 
			"If vtx_type is NOT VERTEX_TYPE_CUSTOM, vtx_size must be zero, is %d",
			params->vtx_size);
		return nullptr;
	}

	uint32_t idx_align = get_idx_type_align(params->idx_type);

	if (reinterpret_cast<uintptr_t>(params->idx_data) % idx_align != 0) {
		log_error("idx_data is misaligned, should be %d", params->vtx_align);
		return nullptr;
	}

	size_t idx_size_bytes = (size_t)params->idx_count * 
		(size_t)get_idx_type_size(params->idx_type);

	void *idx_data = new (std::align_val_t{idx_align}) char[idx_size_bytes];

	uint32_t vtx_size = is_custom ? 
		params->vtx_size : get_vtx_type_size(params->vtx_type);
	uint32_t vtx_align = is_custom ? 
		params->vtx_align : get_vtx_type_align(params->vtx_type);

	if (reinterpret_cast<uintptr_t>(params->vtx_data) % vtx_align != 0) {
		log_error("vtx_data is misaligned, should be %d", params->vtx_align);
		return nullptr;
	}

	uint32_t vtx_count = params->vtx_count;
	size_t vtx_size_bytes = (size_t)vtx_count * (size_t)vtx_size;

	void *vtx_data = new (std::align_val_t{vtx_align}) char[vtx_size_bytes];

	assert(params->idx_data);
	assert(params->vtx_data);

	memcpy(vtx_data, params->vtx_data, vtx_size_bytes);
	memcpy(idx_data, params->idx_data, idx_size_bytes);

	Mesh *mesh = new Mesh {
		.idx_type = params->idx_type, 

		.vtx_type = params->vtx_type,
		.vtx_size = vtx_size, 
		.vtx_align = vtx_align,

		.idx_count = params->idx_count,
		.vtx_count = vtx_count,

		.idx_data = idx_data,
		.vtx_data = vtx_data,
	};

	return mesh;
}

void destroy_mesh_internal(GfxContext *ctx)
{

}

static uint64_t commit_simple_upload(GfxContext *ctx, 
	void *src, size_t size, size_t align, BufferID dst, size_t dst_offset) 
{
	ev2::UploadContext uc = ev2::begin_upload(ctx, size, align); 
	memcpy(uc.ptr, src, size);
	BufferUpload up = {
		.src_offset = 0,
		.dst_offset = dst_offset, 
		.size = size,
	};
	return ev2::commit_buffer_uploads(ctx, uc, dst, &up, 1);
}

Result ensure_mesh_buffers_loaded(GfxContext *ctx, MeshID mesh_id)
{
	Mesh *mesh = ctx->get_mesh(mesh_id);

	// For now, just create buffers owned by the mesh 
	if (mesh->vbo.is_valid() && mesh->ibo.is_valid()) {
		return SUCCESS;
	}

	size_t vbo_size = mesh->vbo_size_bytes();
	size_t ibo_size = mesh->ibo_size_bytes();

	//TODO: Be smarter here and try packing vbo + ibo

	mesh->vbo = ev2::create_buffer(ctx, vbo_size, 
		ev2::BUFFER_USAGE_TRANSFER_SRC_BIT | ev2::BUFFER_USAGE_VERTEX_BUFFER_BIT);
	mesh->vbo_offset = 0;

	mesh->ibo = ev2::create_buffer(ctx, ibo_size, 
		ev2::BUFFER_USAGE_TRANSFER_SRC_BIT | ev2::BUFFER_USAGE_INDEX_BUFFER_BIT);
	mesh->ibo_offset = 0;

	commit_simple_upload(ctx, 
		mesh->vtx_data, vbo_size, 
		mesh->vtx_align, 
		mesh->vbo, mesh->vbo_offset
	);

	commit_simple_upload(ctx, 
		mesh->idx_data, ibo_size, 
		get_idx_type_align(mesh->idx_type), 
		mesh->ibo, mesh->ibo_offset
	);

	return SUCCESS;
}

//------------------------------------------------------------------------------
// Loading interface

static ev2::Result mesh_reload_callback(ev2::GfxContext *ctx, void** usr, const char *path)
{
}

static void mesh_destroy_callback(ev2::GfxContext *ctx, void* usr)
{
}

static Mesh *load_mesh_from_file(const char * path)
{

}

#pragma pack(push, 1)
struct RawTri {
    float nx, ny, nz;
    float v0x, v0y, v0z;
    float v1x, v1y, v1z;
    float v2x, v2y, v2z;
    uint16_t attrib;
};
#pragma pack(pop)

MeshID load_mesh(GfxContext *ctx, const char *path)
{
	AssetID id = ctx->assets->load(path);
	if (id)
		return MeshID{id};

	static AssetVTable vtbl = {
		.reload = mesh_reload_callback,
		.destroy = mesh_destroy_callback,
	};

	std::string sys_path = ctx->assets->get_system_path(path);

	ev2::Mesh *mesh = load_mesh_from_file(sys_path.c_str()); 

	Result result = SUCCESS;

	if (result == SUCCESS) {
		id = ctx->assets->allocate(&vtbl, mesh, path); 
		return MeshID{id};
	}

	return EV2_NULL_HANDLE(Mesh);

}

}
