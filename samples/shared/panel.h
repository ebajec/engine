#ifndef MY_PANEL_H
#define MY_PANEL_H

#include <ev2/device.h>
#include <ev2/render.h>

#include <glm/vec2.hpp>

#include <string>

#include <app.h>

class Panel
{
	ev2::Device *m_dev;

	std::string m_name;

	glm::ivec2 m_pos;
	glm::ivec2 m_size;

	ev2::RenderTargetID m_target = 
		EV2_NULL_HANDLE(RenderTarget);

	ev2::RenderTargetFlags m_target_flags = 
		ev2::RENDER_TARGET_COLOR_BIT | 
		ev2::RENDER_TARGET_DEPTH_BIT;

	bool m_hovered : 1 = false;
	bool m_content_selected : 1 = false;
public:

	ev2::RenderTargetID get_target();

	glm::ivec2 get_size();
	glm::ivec2 get_pos();

	bool is_hovered();
	bool is_content_selected();

	Panel(ev2::Device *dev, const char *name, 
		uint32_t x, uint32_t y, uint32_t w, uint32_t h);

	~Panel();
	void imgui();
};

#endif // MY_PANEL_H
