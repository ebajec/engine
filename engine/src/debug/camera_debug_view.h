#include "renderer/gl_renderer.h"

#include <resource/resource_table.h>
#include <resource/buffer.h>
#include <resource/material_loader.h>

#include <optional>

class CameraDebugView
{
	ResourceTable *m_rt;

	MaterialID frust_material;
	BufferID m_ubo;
	uint32_t m_vao;

	std::optional<Camera> m_camera;
public:
	CameraDebugView(ResourceTable * rt) : m_rt(rt)
	{
		//------------------------------------------------------------------------------
		// Test Camera
		
		m_ubo = create_buffer(m_rt, sizeof(glm::mat4));

		static uint32_t frust_indices[] = {
			0,1, 0,2, 2,3, 3,1, 
			0,4, 1,5, 2,6, 3,7, 
			4,5, 4,6, 6,7, 7,5
		};

		BufferID test_ibo = create_buffer(m_rt, sizeof(frust_indices));
		LoadResult result = upload_buffer(m_rt, test_ibo, frust_indices, sizeof(frust_indices));
		if (result)
			return;

		frust_material = load_material_file(m_rt, "material/frustum.yaml");

		if (!frust_material)
			return;

		glGenVertexArrays(1,&m_vao);
		glBindVertexArray(m_vao);

		const GLBuffer *test_ibo_data = m_rt->get<GLBuffer>(test_ibo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, test_ibo_data->id);

		glBindVertexArray(0);
	}

	void set_camera(const Camera* camera) {
		if (camera)
			m_camera = *camera;
		else 
			m_camera.reset();
	}

	const Camera *get_camera() {
		return m_camera ? &m_camera.value() : nullptr;
	}

	void render(const RenderContext& ctx)
	{ 
		glm::mat4 pv = glm::mat4(0);
		if (!m_camera) {
			return;
		} else {
			pv = m_camera->proj*m_camera->view;
		}

		LoadResult result = upload_buffer(m_rt, m_ubo, &pv, sizeof(pv));

		if (result)
			return;

		ctx.bind_material(frust_material);

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_rt->get<GLBuffer>(m_ubo)->id);
		glBindVertexArray(m_vao);
		glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT,nullptr);
		glBindVertexArray(0);

	}
};

