#include <ev2/context.h>
#include <ev2/render.h>
#include <ev2/utils/geometry.h>

#include <glm/mat4x4.hpp>

class CameraDebugView
{
	Camera m_camera = {
		.proj = glm::mat4(1.f), 
		.view = glm::mat4(1.f)
	};

	ev2::GfxContext *ctx;

	ev2::GfxPipelineID pipeline;
	ev2::DescriptorSetID desc;

	ev2::BufferID ssbo;
	ev2::BufferID ibo;

	uint32_t m_vao;

	void *mapped;
public:
	CameraDebugView(ev2::GfxContext *ctx);
	~CameraDebugView();
	void render(const ev2::PassCtx& ctx);
	void set_camera(const Camera *camera);
	const Camera *get_camera();
};

