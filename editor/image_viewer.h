#ifndef IMAGE_VIEWER_H
#define IMAGE_VIEWER_H

#include "viewport.h"

#include <ev2/pipeline.h>
#include <ev2/resource.h>

#include <glm/mat4x4.hpp>

struct ImageViewer2 : public Viewport2
{
	std::string pipeline_path;

	ev2::ImageID image;
	uint32_t level = 0;
	uint32_t layer = 0;

	struct RenderData {
		ev2::TextureID tex {};
		ev2::TextureFilter filter = ev2::FILTER_NEAREST;

		ev2::GfxPipelineID pipeline {};
		ev2::BindingsID bindings {};

		glm::vec2 center = glm::vec2(0);

		glm::mat4 proj = glm::mat4(1.f);
		glm::mat4 view = glm::mat4(1.f);
		ev2::ViewID camera;

		float zoom = 1.f;
	} rd;

	ImageViewer2(uint32_t x, uint32_t y, uint32_t w, uint32_t h, 
					const char * pipeline = "core://pipeline/screen_quad.yaml", const char *name = nullptr);
	~ImageViewer2();

	int set_pipeline(const char *path);

	void set_texture_filter(ev2::TextureFilter filter);

	int set_image(ev2::GfxContext *ctx, ev2::ImageID img, uint32_t lvl, uint32_t lyr);

	int init(ev2::GfxContext *ctx, ev2::ImageID img);
	int update(ev2::GfxContext *ctx);

	// Render into the underlying viewport's target, clearing old values
	void render(ev2::GfxContext *ctx);
	void destroy(ev2::GfxContext *ctx);
	void record_draw(ev2::PassID pass);

	glm::vec2 get_grid_cursor_pos();
};

#endif // IMAGE_VIEWER_H
