#ifndef EV2_VIEWPORT_H
#define EV2_VIEWPORT_H

#include "ev2/camera_controller.h"

#include <ev2/context.h>
#include <ev2/pipeline.h>

#include <glm/mat2x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <string>
#include <vector>
#include <functional>
#include <memory>

// A GUI window with it's own render target that gets
// displayed inside each frame
class Viewport
{
	std::vector<std::function<void()>> settings_callbacks;

	std::string m_name;

	glm::ivec2 m_pos;
	glm::ivec2 m_size = glm::ivec2(0);

	glm::mat4 m_screen_to_world = glm::mat4(1.f);

	// A global viewport index obtained from a global monotonic counter.
	uint32_t m_id;

	ev2::RenderTargetID m_target = 
		EV2_NULL_HANDLE(RenderTarget);

	std::weak_ptr<ICameraController> m_cam;

	ev2::ViewID m_view;

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

	Viewport &extend_settings(std::function<void()>&& callback);
	void set_closable(bool closable) {m_closable = closable;}
	void set_camera(std::shared_ptr<ICameraController> camera);

	bool is_content_selected();
	bool is_focused() { return m_focused; }
	bool is_hovered() {return m_hovered; }

	glm::ivec2 get_size() const { return m_size; }
	glm::ivec2 get_pos() const { return m_pos; }
	const char* get_name() { return m_name.c_str(); }
	uint32_t get_id() const { return m_id; }
	ev2::ViewID get_view() const { return m_view; }
	const glm::mat4 &get_screen_to_world() const { return m_screen_to_world; }

	Viewport(const char *name, uint32_t x, uint32_t y, uint32_t w, uint32_t h, 
	   	ev2::RenderTargetFlags flags = ev2::RENDER_TARGET_CREATE_COLOR_BIT);

	~Viewport();

	int imgui(int *p_flags);
};

#endif // EV2_VIEWPORT_H
