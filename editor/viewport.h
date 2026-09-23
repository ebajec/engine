#ifndef VIEWPORT_H
#define VIEWPORT_H

#include <ev2/context.h>
#include <ev2/pipeline.h>

#include <glm/mat2x3.hpp>
#include <glm/vec2.hpp>

#include <string>
#include <vector>
#include <functional>

// A GUI window with it's own render target that gets
// displayed inside each frame
class Viewport2
{
	std::vector<std::function<void()>> settings_callbacks;

	std::string m_name;

	glm::ivec2 m_pos;
	glm::ivec2 m_size = glm::ivec2(0);

	// A global viewport index obtained from a global monotonic counter.
	// Used for ImGui
	uint32_t m_id;

	ev2::RenderTargetID m_target = 
		EV2_NULL_HANDLE(RenderTarget);

	ev2::RenderTargetFlags m_target_flags;

	bool m_needs_resize : 1 = false;
	bool m_hovered : 1 = false;
	bool m_content_hovered : 1 = false;
	bool m_focused : 1 = false;
	bool m_closable : 1 = true;

	void cleanup_render_target();
	int update(int *p_flags);

public:
	enum UpdateFlagBits {
		SHOULD_CLOSE_BIT = 0x1,
		RESIZED_BIT = 0x2
	};

	VkDescriptorSet imgui_texture = VK_NULL_HANDLE;

	ev2::RenderTargetID get_target();

	const char* get_name() {
		return m_name.c_str();
	}

	Viewport2 &extend_settings(std::function<void()>&& callback);

	glm::ivec2 get_size();
	glm::ivec2 get_pos();

	bool is_focused();
	bool is_hovered();
	bool is_content_selected();

	void set_closable(bool closable) {m_closable = closable;}

	uint32_t get_id() {return m_id;}

	Viewport2(const char *name, uint32_t x, uint32_t y, uint32_t w, uint32_t h, 
	   	ev2::RenderTargetFlags flags = ev2::RENDER_TARGET_CREATE_COLOR_BIT);

	~Viewport2();

	int imgui(int *p_flags);
};

#endif // VIEWPORT_H
