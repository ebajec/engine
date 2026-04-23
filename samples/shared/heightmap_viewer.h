#ifndef HEIGHTMAP_PANEL_H
#define HEIGHTMAP_PANEL_H

#include "app.h"
#include "panel.h"

#include <ev2/utils/camera.h>

#include <ev2/render.h>

#include <memory>

struct HeightmapViewerPanel
{
	App *app;
	std::unique_ptr<Panel> panel;

	MotionCamera control;
	glm::vec3 keydir = glm::vec3(0,0,0);

	struct RenderData {
		glm::mat4 proj = glm::mat4(1.f);
		glm::mat4 view = glm::mat4(1.f);

		uint32_t w = 0, h = 0;
		ev2::TextureID tex;

		ev2::ViewID camera;
		ev2::BufferID ibo; 
		ev2::GraphicsPipelineID pipeline;
		ev2::DescriptorSetID descriptors;
	} rd;

	int set_texture(ev2::Device *dev, ev2::TextureID tex);

	int init(App *app, ev2::Device *dev, ev2::TextureID tex);
	int update(ev2::Device *dev);
	void render(ev2::Device *dev);
	void destroy(ev2::Device *dev);
};

#endif //HEIGHTMAP_PANEL_H
