#ifndef IMAGE_VIEWER_H
#define IMAGE_VIEWER_H

#include "viewport.h"

#include <ev2/pipeline.h>
#include <ev2/resource.h>

#include <glm/mat4x4.hpp>

class ImageViewer2 : public Viewport2
{
public:
	enum FlagsBits 
	{
		// Whether the editor will keep a pointer and call update each frame.
		// Always set for viewers created by the editor.  If this is set and
		// a shared pointer is held somewhere, the viewer will not actually
		// close until that reference is gone.  Use std::weak_ptr if not
		// calling update and catching the SHOULD_CLOSE manually.
		EDITOR_OWNED_BIT = 0x1,

		// Whether the editor will render the image each frame via "render()"
		AUTO_RENDERED_BIT = 0x2
	};
private:
	std::string pipeline_path;

	ev2::ImageID image;
	uint32_t level = 0;
	uint32_t layer = 0;

	uint32_t flags = 0;

	struct RenderData {
		ev2::TextureID tex {};
		ev2::TextureFilter filter = ev2::FILTER_NEAREST;

		ev2::GfxPipelineID pipeline {};
		ev2::BindingsID bindings {};

		glm::vec2 center = glm::vec2(0);

		glm::mat4 proj = glm::mat4(1.f);
		glm::mat4 view = glm::mat4(1.f);
		ev2::ViewID camera = {};

		float zoom = 1.f;
	} rd;

	void destroy(ev2::GfxContext *ctx);
public:

	ImageViewer2(
		uint32_t x,
		uint32_t y,
		uint32_t w,
		uint32_t h, 
		uint32_t flags = 0,
		const char * pipeline = "core://pipeline/screen_quad.yaml",
		const char *name = nullptr
	);
	~ImageViewer2();

	int update(ev2::GfxContext *ctx);

	constexpr bool is_auto_rendered() const {return flags & AUTO_RENDERED_BIT;}

	int set_pipeline(const char *path);
	void set_texture_filter(ev2::TextureFilter filter);
	int set_image(ev2::GfxContext *ctx, ev2::ImageID img, uint32_t lvl, uint32_t lyr);

	glm::vec2 get_grid_cursor_pos();

	// Render into the underlying viewport's target, clearing old values
	void render(ev2::GfxContext *ctx);
	void record_draw(ev2::PassID pass);
};

#endif // IMAGE_VIEWER_H
