#ifndef HEIGHTMAP_VIEW_H
#define HEIGHTMAP_VIEW_H

#include "ev2/viewport.h"
#include "ev2/motion_camera.h"

#include <memory>

class HeightmapViewer
{
	std::unique_ptr<Viewport> m_viewport;
	std::shared_ptr<MotionCamera> m_camera;

	struct Uniforms {
		float scale = 1.f;
	} m_uniforms;

	struct RenderData {
		uint32_t w = 0, h = 0;
		ev2::TextureID tex;
		ev2::BufferID ibo;
		ev2::GfxPipelineID pipeline;
		ev2::BindingsID bindings;
	} rd;

	void destroy(ev2::GfxContext *ctx);
public:
	HeightmapViewer(ev2::TextureID tex = {});
	~HeightmapViewer();

	int set_texture(ev2::GfxContext *ctx, ev2::TextureID tex);

	// @param p_flags 
	int update(ev2::GfxContext *ctx, int *p_flags = nullptr);

	void render(ev2::GfxContext *ctx);

	Viewport *viewport() { return m_viewport.get(); }
	MotionCamera *camera() { return m_camera.get(); }
};

#endif //HEIGHTMAP_VIEW_H
