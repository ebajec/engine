#ifndef EV2_EDITOR_H
#define EV2_EDITOR_H

#include <ev2/context.h>
#include <ev2/resource.h>
#include <ev2/pipeline.h>

// glfw
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// glm
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

// imgui
#include <imgui.h>
#include <implot.h>

//stl
#include <memory>

class ImageViewer2;

namespace Editor
{
	enum {
		OK = 0,
		ERROR = -1,
		SHOULD_CLOSE = 1
	};

	struct InputData
	{
		// for wasd + shift + space camera movement
		glm::vec3 move_dir; 

		glm::dvec2 mouse_pos[2];
		glm::dvec2 scroll;
		glm::dvec2 scroll_delta;

		double t0;
		double t1;
		double dt;

		int mouse_mode = GLFW_CURSOR_NORMAL;

		bool mouse_in_gui : 1 = false;

		bool left_mouse_pressed : 1 = false;
		bool right_mouse_pressed : 1 = false;

		bool needs_resize : 1 = true;

		glm::dvec2 get_mouse_delta() const {
			return mouse_pos[0] - mouse_pos[1];
		}

		ImGuiID dockspace_id = 0;
	};

	struct WindowData {
		GLFWwindow *ptr;
		int width;
		int height;
		const char *title;
	};

	int init(int argc, char *argv[], const char *title = "Editor", int w = 800, int h = 800);
	int begin_frame();
	int end_frame();
	void shutdown();

	ev2::GfxContext *ctx();
	const InputData &input();
	WindowData &window();

	bool should_close();
	ev2::PassID gui_pass();

	std::shared_ptr<ImageViewer2> open_image_viewer(
		ev2::ImageID image,
		const char *name,
		const char *pipeline,
		bool auto_render = true,
		uint32_t w = 500,
		uint32_t h = 500
	);
}

#endif //EV2_EDITOR_H
