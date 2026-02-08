#ifndef EV2_APP_H
#define EV2_APP_H

#include <ev2/device.h>

// glfw
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// glm
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <implot.h>

#include <functional>

struct InputData
{
	glm::dvec2 mouse_pos[2];
	glm::dvec2 scroll;

	double t0;
	double t1;
	double dt;

	int mouse_mode = GLFW_CURSOR_NORMAL;

	bool mouse_in_gui : 1 = false;

	bool left_mouse_pressed : 1 = false;
	bool right_mouse_pressed : 1 = false;

	bool needs_resize : 1 = true;

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
	typedef std::function<void(int, int, int, int)> key_callback_t;
	typedef std::function<void(int, int, int)> mouse_button_callback_t;
	typedef std::function<void(double, double)> scroll_callback_t;
	typedef std::function<void(double, double)> cursor_position_callback_t;
	typedef std::function<void(int, int)> framebuffer_size_callback_t;


	enum State {
		OK = 0,
		ERROR = -1,
		SHOULD_CLOSE = 1
	};

	uint64_t frame_counter = 0;

	InputData input;
	WindowData win;

	ev2::Device *dev;

	std::vector<key_callback_t> key_callbacks;

	//-----------------------------------------------------------------------------
	
	int resize(int width, int height);

	int update();

	void begin_frame();
	void end_frame();
	void imgui();

	void update_input();

	int initialize(int argc, char *argv[]);
	void terminate();

	//-----------------------------------------------------------------------------
	// GLFW callbacks
	
	void insert_key_callback(key_callback_t callback) {
		key_callbacks.push_back(callback);
	}
	
	static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
	static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
	static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
	static void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos);
	static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
};

static inline void glfw_wasd_to_motion(glm::dvec3& dir, int key, int action) 
{
	static const int key_fwd = GLFW_KEY_W;
	static const int key_bkwd = GLFW_KEY_S; 
	static const int key_left = GLFW_KEY_A;
	static const int key_right = GLFW_KEY_D;
	static const int key_up = GLFW_KEY_SPACE;
	static const int key_down = GLFW_KEY_LEFT_SHIFT;

	if (key == key_fwd && action == GLFW_PRESS) 
		dir += glm::vec3(1,0,0);
	if (key == key_right && action == GLFW_PRESS) 
		dir += glm::vec3(0,1,0);
	if (key == key_up && action == GLFW_PRESS) 
		dir += glm::vec3(0,0,1);
	if (key == key_bkwd && action == GLFW_PRESS) 
		dir += glm::vec3(-1,0,0);
	if (key == key_left && action == GLFW_PRESS) 
		dir += glm::vec3(0,-1,0);
	if (key == key_down && action == GLFW_PRESS) 
		dir += glm::vec3(0,0,-1);

	if (key == key_fwd && action == GLFW_RELEASE) 
		dir -= glm::vec3(1,0,0);
	if (key == key_right && action == GLFW_RELEASE) 
		dir -= glm::vec3(0,1,0);
	if (key == key_up && action == GLFW_RELEASE) 
		dir -= glm::vec3(0,0,1);
	if (key == key_bkwd && action == GLFW_RELEASE) 
		dir -= glm::vec3(-1,0,0);
	if (key == key_left && action == GLFW_RELEASE) 
		dir -= glm::vec3(0,-1,0);
	if (key == key_down && action == GLFW_RELEASE) 
		dir -= glm::vec3(0,0,-1);
}

static inline void plot_frame_times(float delta)
{
	delta *= 1000.f;
	static std::vector<float> times;
	static std::vector<float> deltas;
	static int scroll = 0;
	static size_t samples = 500;
	static float avg = 0;

	if (deltas.size() < samples) 
		deltas.resize(samples,0);
	if (times.size() < samples) 
		times.resize(samples,0);

	deltas[scroll] = (float)delta;
	times[scroll] = glfwGetTime();

	scroll = (scroll + 1)%samples;

	avg = 0.99*avg + 0.01*delta;

	if (ImPlot::BeginPlot("Frame times (ms)",ImVec2(200,200))) {
		ImPlot::SetupAxesLimits(times[scroll], 
		   times[scroll  ? scroll - 1 : samples - 1], 
		   0, 2*avg,ImPlotCond_Always);
		ImPlot::PlotLine("time", times.data(), 
			deltas.data(), samples, ImPlotCond_Always, scroll);
		ImPlot::EndPlot();
	}
}



#endif
