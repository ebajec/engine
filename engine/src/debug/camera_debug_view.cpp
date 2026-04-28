#include "camera_debug_view.h"

#include "backends/vulkan/context_impl.h"

#include <glm/gtc/type_ptr.hpp>

#include <cstring>

CameraDebugView::CameraDebugView(ev2::GfxContext *_ctx) : ctx(_ctx)
{
	//------------------------------------------------------------------------------
	// Test Camera

	static uint32_t frust_indices[] = {
		0,1, 0,2, 2,3, 3,1, 
		0,4, 1,5, 2,6, 3,7, 
		4,5, 4,6, 6,7, 7,5
	};

	ibo = ev2::create_buffer(ctx, sizeof(frust_indices));

	ssbo = ev2::create_buffer(ctx, sizeof(glm::mat4), 
						  ev2::MAP_WRITE | ev2::MAP_COHERENT | ev2::MAP_PERSISTENT);

	mapped = glMapNamedBufferRange(ctx->get_buffer(ssbo)->id, 0, sizeof(glm::mat4),
						GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT); 

	pipeline = ev2::load_graphics_pipeline(ctx, "pipelines/frustum.yaml");

	ev2::DescriptorLayoutID layout = ev2::get_graphics_pipeline_layout(ctx, pipeline);
	desc = ev2::create_descriptor_set(ctx, layout);

	ev2::BindingSlot slot = ev2::find_binding(layout, "Cameras");
	ev2::bind_buffer(ctx, desc, slot, ssbo, 0, sizeof(glm::mat4));

	glGenVertexArrays(1,&m_vao);

	ev2::UploadContext uc = ev2::begin_upload(ctx, sizeof(frust_indices), alignof(uint32_t));
	memcpy(uc.ptr, frust_indices, sizeof(frust_indices));
	ev2::BufferUpload up = {
		.src_offset = 0,
		.dst_offset = 0,
		.size = sizeof(frust_indices)
	};
	uint64_t sync = ev2::commit_buffer_uploads(ctx, uc, ibo, &up, 1);
	ev2::flush_uploads(ctx);

	ev2::wait_complete(ctx, sync);

}

CameraDebugView::~CameraDebugView()
{
	glDeleteVertexArrays(1, &m_vao);
	glUnmapNamedBuffer(ctx->get_buffer(ssbo)->id);

	ev2::destroy_buffer(ctx, ssbo);
	ev2::destroy_buffer(ctx, ibo);
	ev2::destroy_descriptor_set(ctx, desc);
}

void CameraDebugView::set_camera(const Camera *camera) {
	if (camera)
		m_camera = *camera;
}

const Camera *CameraDebugView::get_camera() {
	return &m_camera;
}

void CameraDebugView::render(const ev2::PassCtx& pass)
{ 
	glm::mat4 pv = m_camera.proj*m_camera.view;

	memcpy(mapped, glm::value_ptr(pv), sizeof(glm::mat4));

	ev2::cmd_bind_gfx_pipeline(pass.rec, pipeline);
	ev2::cmd_bind_descriptor_set(pass.rec, desc);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->get_buffer(ibo)->id);
	glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT,nullptr);
}
