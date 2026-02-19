#ifndef TEXTURE_VIEWER_H
#define TEXTURE_VIEWER_H

#include "app.h"
#include "panel.h"

#include <ev2/render.h>
#include <ev2/pipeline.h>
#include <ev2/resource.h>

#include <memory>

struct TextureViewerPanel
{
	App *app;

	std::unique_ptr<Panel> panel;

	struct RenderData {
		ev2::TextureID map;

		ev2::GraphicsPipelineID screen_quad;
		ev2::DescriptorSetID screen_quad_set;

		ev2::BindingSlot tex_slot;

		glm::vec2 center = glm::vec2(0);

		glm::mat4 proj = glm::mat4(1.f);
		glm::mat4 view = glm::mat4(1.f);
		ev2::ViewID camera;

		float zoom = 1.f;
	} rd;

	glm::vec2 world_cursor;
	TextureViewerPanel(App *app, uint32_t x, uint32_t y, uint32_t w, uint32_t h);

	int set_texture(ev2::Device *dev, ev2::TextureID tex);

	int init(ev2::Device *dev, ev2::TextureID tex);
	int update(ev2::Device *dev);
	void render(ev2::Device *dev);
	void destroy(ev2::Device *dev);

	glm::vec2 get_world_cursor_pos();
};

#endif // TEXTURE_VIEWER_H
