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

	ibo = ev2::create_buffer(ctx, sizeof(frust_indices), ev2::BUFFER_USAGE_INDEX_BUFFER_BIT);
	ssbo = ev2::create_buffer(ctx, sizeof(glm::mat4), ev2::BUFFER_USAGE_STORAGE_BUFFER_BIT);

	pipeline = ev2::load_graphics_pipeline(ctx, "pipelines/frustum.yaml");

	desc = ev2::create_bindings(ctx, pipeline, EV2_GFX_SET_PER_DRAW);

	ev2::bind_buffer(ctx, desc, "Camera", ssbo, 0, sizeof(glm::mat4));

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

	ev2::destroy_buffer(ctx, ssbo);
	ev2::destroy_buffer(ctx, ibo);
	ev2::destroy_bindings(ctx, desc);
}

void CameraDebugView::set_camera(const Camera *camera) {
	if (camera)
		m_camera = *camera;
}

const Camera *CameraDebugView::get_camera() {
	return &m_camera;
}

void CameraDebugView::render(const ev2::PassID& pass)
{ 
	glm::mat4 pv = m_camera.proj*m_camera.view;

	memcpy(mapped, glm::value_ptr(pv), sizeof(glm::mat4));

	ev2::cmd_bind_gfx_pipeline(pass, pipeline);
	ev2::cmd_bind_resources(pass, desc);

	//glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->get_buffer(ibo)->id);
	glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT,nullptr);
}
