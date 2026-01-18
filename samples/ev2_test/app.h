#ifndef EV2_APP_H
#define EV2_APP_H

#include <ev2/device.h>

// glfw
#include <GLFW/glfw3.h>

// glm
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>

struct InputData
{
	glm::dvec2 mouse_pos[2];

	bool mouse_in_gui = false;
	bool left_mouse_pressed = false;
	bool right_mouse_pressed = false;

	glm::dvec2 get_mouse_delta() {
		return mouse_pos[0] - mouse_pos[1];
	}
};

struct WindowData 
{
	GLFWwindow *ptr;
	int width;
	int height;
	const char *title;
};

struct App
{
	enum State {
		OK = 0,
		ERROR = -1,
		SHOULD_CLOSE = 1
	};

	uint64_t frame_counter = 0;

	InputData input;
	WindowData win;

	ev2::Device *dev;

	//-----------------------------------------------------------------------------
	
	int resize(int width, int height);
	int update();

	void update_input();

	int initialize();
	void terminate();

	//-----------------------------------------------------------------------------
	// GLFW callbacks
	
	static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
	static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
	static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
	static void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos);
	static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
};

#endif
