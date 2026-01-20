#include "engine/renderer/opengl.h"
#include "camera_debug_view.h"

#include "device_impl.h"

#include <glm/gtc/type_ptr.hpp>

#include <cstring>

CameraDebugView::CameraDebugView(ev2::Device *_dev) : dev(_dev)
{
	//------------------------------------------------------------------------------
	// Test Camera

	static uint32_t frust_indices[] = {
		0,1, 0,2, 2,3, 3,1, 
		0,4, 1,5, 2,6, 3,7, 
		4,5, 4,6, 6,7, 7,5
	};

	ibo = ev2::create_buffer(dev, sizeof(frust_indices));

	ssbo = ev2::create_buffer(dev, sizeof(glm::mat4), 
						  ev2::MAP_WRITE | ev2::MAP_COHERENT | ev2::MAP_PERSISTENT);

	mapped = glMapNamedBufferRange(dev->get_buffer(ssbo)->id, 0, sizeof(glm::mat4),
						GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT); 

	pipeline = ev2::load_graphics_pipeline(dev, "pipelines/frustum.yaml");

	ev2::DescriptorLayoutID layout = ev2::get_graphics_pipeline_layout(dev, pipeline);
	desc = ev2::create_descriptor_set(dev, layout);

	ev2::BindingSlot slot = ev2::find_binding(layout, "Cameras");
	ev2::bind_buffer(dev, desc, slot, ssbo, 0, sizeof(glm::mat4));

	glGenVertexArrays(1,&m_vao);

	ev2::UploadContext uc = ev2::begin_upload(dev, sizeof(frust_indices), alignof(uint32_t));
	memcpy(uc.ptr, frust_indices, sizeof(frust_indices));
	ev2::BufferUpload up = {
		.src_offset = 0,
		.dst_offset = 0,
		.size = sizeof(frust_indices)
	};
	uint64_t sync = ev2::commit_buffer_uploads(dev, uc, ibo, &up, 1);
	ev2::flush_uploads(dev);

	ev2::wait_complete(dev, sync);

}

CameraDebugView::~CameraDebugView()
{
	glDeleteVertexArrays(1, &m_vao);
	glUnmapNamedBuffer(dev->get_buffer(ssbo)->id);

	ev2::destroy_buffer(dev, ssbo);
	ev2::destroy_buffer(dev, ibo);
	ev2::destroy_descriptor_set(dev, desc);
}

void CameraDebugView::set_camera(const Camera *camera) {
	if (camera)
		m_camera = *camera;
}

const Camera *CameraDebugView::get_camera() {
	return &m_camera;
}

void CameraDebugView::render(const ev2::PassCtx& ctx)
{ 
	glm::mat4 pv = m_camera.proj*m_camera.view;

	memcpy(mapped, glm::value_ptr(pv), sizeof(glm::mat4));

	ev2::cmd_bind_pipeline(ctx.rec, pipeline);
	ev2::cmd_bind_descriptor_set(ctx.rec, desc);

	glBindVertexArray(m_vao);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dev->get_buffer(ibo)->id);
	glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT,nullptr);
	glBindVertexArray(0);
}
