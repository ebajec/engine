#ifndef MY_PANEL_H
#define MY_PANEL_H

#include <ev2/context.h>
#include <ev2/pipeline.h>

#include <glm/mat2x3.hpp>
#include <glm/vec2.hpp>

#include <string>

#include <app.h>

class Panel
{
	App *m_app;
	ev2::GfxContext *m_ctx;

	std::function<void()> settings_callback;

	std::string m_name;
	std::string m_settings_name;

	glm::ivec2 m_pos;
	glm::ivec2 m_size;

	ev2::RenderTargetID m_target = 
		EV2_NULL_HANDLE(RenderTarget);

	ev2::RenderTargetFlags m_target_flags;

	bool m_hovered : 1 = false;
	bool m_content_hovered : 1 = false;
	bool m_focused : 1 = false;
	bool m_bar_selected : 1 = false;
	bool m_closable : 1 = true;
public:
	VkDescriptorSet imgui_texture = VK_NULL_HANDLE;

	ev2::RenderTargetID get_target();

	const char* get_name() {
		return m_name.c_str();
	}

	void set_settings(std::function<void()>&& callback) {settings_callback = callback;}

	glm::ivec2 get_size();
	glm::ivec2 get_pos();

	bool is_focused();
	bool is_hovered();
	bool is_content_selected();

	void set_closable(bool closable) {m_closable = closable;}

	Panel(App *app, ev2::GfxContext *ctx, const char *name, 
		uint32_t x, uint32_t y, uint32_t w, uint32_t h, 
	   	ev2::RenderTargetFlags flags = ev2::RENDER_TARGET_CREATE_COLOR_BIT);

	~Panel();
	 bool imgui();
};

#endif // MY_PANEL_H
