#include "ev2/core.h"

#include "backends/vulkan/context.h"
#include "utils/asset_table.h"

#include "mesh.h"

namespace ev2 {

static bool validate_geometry(const MeshGeometry *geom)
{
	const bool is_custom = geom->vtx_format == VERTEX_FORMAT_CUSTOM; 

	if (is_custom && geom->vtx_size == 0) {
		log_error( 
			"If vtx_type is VERTEX_FORMAT_CUSTOM, vtx_size (%d) and vtx_align (%d) must be nonzero",
			geom->vtx_size, geom->vtx_align);
		return false;
	}

	if (!is_custom && geom->vtx_size != 0) {
		log_error( 
			"If vtx_type is NOT VERTEX_FORMAT_CUSTOM, vtx_size must be zero, is %d",
			geom->vtx_size);
		return false;
	}

	uint32_t idx_align = get_idx_type_align(geom->idx_type);

	if (reinterpret_cast<uintptr_t>(geom->idx_data) % idx_align != 0) {
		log_error("idx_data is misaligned, should be %d", geom->vtx_align);
		return false;
	}

	uint32_t vtx_size = geom->vtx_size ? 
		geom->vtx_size : get_vtx_type_size(geom->vtx_format);
	uint32_t vtx_align = geom->vtx_align ? 
		geom->vtx_align : get_vtx_type_align(geom->vtx_format);

	if (reinterpret_cast<uintptr_t>(geom->vtx_data) % vtx_align != 0) {
		log_error("vtx_data is misaligned, should be %d", geom->vtx_align);
		return false;
	}

	return true;
}

MeshID create_mesh(GfxContext *ctx, const MeshGeometry *geom)
{
	if (!validate_geometry(geom))
		return EV2_NULL_HANDLE(Mesh);

	size_t idx_size_bytes = (size_t)geom->idx_count * 
		(size_t)get_idx_type_size(geom->idx_type);

	void *idx_data = new (std::align_val_t{get_idx_type_align(geom->idx_type)}) char[idx_size_bytes];

	uint32_t vtx_size = geom->vtx_size ? 
		geom->vtx_size : get_vtx_type_size(geom->vtx_format);
	uint32_t vtx_align = geom->vtx_align ? 
		geom->vtx_align : get_vtx_type_align(geom->vtx_format);
	uint32_t vtx_count = geom->vtx_count;
	size_t vtx_size_bytes = (size_t)vtx_count * (size_t)vtx_size;

	void *vtx_data = ::operator new (vtx_size_bytes, std::align_val_t{vtx_align});

	assert(geom->idx_data);
	assert(geom->vtx_data);

	memcpy(vtx_data, geom->vtx_data, vtx_size_bytes);
	memcpy(idx_data, geom->idx_data, idx_size_bytes);

	Mesh mesh = {
		.geom = *geom
	};

	mesh.geom.vtx_size = vtx_size;
	mesh.geom.vtx_align = vtx_align;
	mesh.geom.vtx_data = vtx_data;
	mesh.geom.idx_data = idx_data;

	MeshID id = ctx->emplace_mesh(std::move(mesh));
	uint32_t gpu_idx = ctx->mesh_info.add(MeshInfoGPU{});
	assert(gpu_idx = id.id);

	return id;
}

void destroy_mesh(GfxContext *ctx, MeshID id)
{
	Mesh *mesh = ctx->get_mesh(id);
	::operator delete[] (mesh->geom.vtx_data, std::align_val_t{mesh->geom.vtx_align});
	::operator delete[] (mesh->geom.idx_data, std::align_val_t{get_idx_type_align(mesh->geom.idx_type)});

	ev2::destroy_buffer(ctx, mesh->buffer);

	ctx->mesh_pool->deallocate(to_pool_id(id));
}

static void fill_gpu_hdr(MeshBufferHeaderGPU *hdr, const Mesh* mesh)
{
	hdr->format = mesh->geom.vtx_format;
	hdr->count = mesh->geom.vtx_count;

	memcpy(hdr->uv_scale, mesh->geom.uv_scale, sizeof(hdr->uv_scale));
	memcpy(hdr->uv_offset, mesh->geom.uv_offset, sizeof(hdr->uv_offset));
}

Result mesh_ensure_loaded_gpu(GfxContext *ctx, MeshID mesh_id)
{
	Mesh *mesh = ctx->get_mesh(mesh_id);

	// For now, just create buffers owned by the mesh 
	if (mesh->buffer.is_valid()) {
		return SUCCESS;
	}

	size_t hdr_start = 0, vbo_start, ibo_start;
	size_t bufsize = mesh->prepare_offsets(hdr_start, &vbo_start, &ibo_start);

	mesh->buffer_offset = hdr_start;
	mesh->buffer = ev2::create_buffer(ctx, bufsize, 
		ev2::BUFFER_USAGE_TRANSFER_SRC_BIT | ev2::BUFFER_USAGE_VERTEX_BUFFER_BIT);

	//---------------------------------------------------------------------------------------
	// update table
	
	glm::vec3 extent = glm::make_vec3(mesh->geom.pos_extent);
	glm::vec3 offset = glm::make_vec3(mesh->geom.pos_offset);

	ctx->mesh_info.set(mesh_id.id, MeshInfoGPU{
		.pos_extent = {extent.x, extent.y, extent.z},
		.first_index = 0,
		.pos_offset = {offset.x, offset.y, offset.z},
		.index_count = mesh->geom.idx_count,
		.buffer = ev2::get_buffer_device_address(ctx, mesh->buffer),
	});

	//---------------------------------------------------------------------------------------
	// upload

	size_t vbo_size = mesh->vbo_size_bytes();
	size_t ibo_size = mesh->ibo_size_bytes();
	
	ev2::UploadContext uc = ev2::begin_upload(ctx, bufsize, 16); 

	MeshBufferHeaderGPU gpu_hdr;
	fill_gpu_hdr(&gpu_hdr, mesh);

	memcpy(uc.ptr, &gpu_hdr, sizeof(gpu_hdr));
	memcpy((char*)uc.ptr + vbo_start, mesh->geom.vtx_data, vbo_size);
	memcpy((char*)uc.ptr + ibo_start, mesh->geom.idx_data, ibo_size);

	BufferUpload up = {
		.src_offset = 0,
		.dst_offset = hdr_start, 
		.size = bufsize,
	};

	ev2::commit_buffer_uploads(ctx, uc, mesh->buffer, &up, 1);

	return SUCCESS;
}

//------------------------------------------------------------------------------
// Loading interface

static ev2::Result model_reload_callback(ev2::GfxContext *ctx, void** usr, const char *path)
{
	// TODO...
	assert(false);
	return ev2::SUCCESS;
}

static void model_destroy_callback(ev2::GfxContext *ctx, void* usr)
{
	// TODO...
	assert(false);
}

static Mesh *model_create_callback(const char * path)
{
	// TODO...
	assert(false);
	return nullptr;
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

ModelID load_model(GfxContext *ctx, const char *path)
{
	AssetID id;
	ev2::Result result = ctx->assets->load(path, &id);

	if (result < ev2::SUCCESS)
		return EV2_NULL_HANDLE(Model);

	if (id)
		return ModelID{id};

	static AssetVTable vtbl = {
		.reload = model_reload_callback,
		.destroy = model_destroy_callback,
	};

	ev2::Mesh *mesh = model_create_callback(path); 

	if (result == SUCCESS) {
		id = ctx->assets->allocate(&vtbl, mesh, path); 
		return ModelID{id};
	}

	return EV2_NULL_HANDLE(Model);

}

}
