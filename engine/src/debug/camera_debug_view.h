#include "renderer/renderer.h"

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
	CameraDebugView(ResourceTable * rt);
	void render(const RenderContext& ctx);
	void set_camera(const Camera* camera);
	const Camera *get_camera();
};

