#ifndef APP_WINDOW_H
#define APP_WINDOW_H

#include "engine/renderer/renderer.h"

#include <GLFW/glfw3.h>

#include <functional>
#include <memory>

typedef std::function<void(int, int, int, int)> key_callback_t;
typedef std::function<void(int, int, int)> mouse_button_callback_t;
typedef std::function<void(double, double)> scroll_callback_t;
typedef std::function<void(double, double)> cursor_position_callback_t;
typedef std::function<void(int, int)> framebuffer_size_callback_t;

extern struct AppGlobals
{
	bool left_mouse_pressed = false;
	bool right_mouse_pressed = false;
	bool mouse_in_gui = false;
	int mouse_mode = GLFW_CURSOR_NORMAL;

	double t0 = -1;
	double t1 = 0;
	double dt = 0;
} g_;

struct MyApp;

struct AppComponent
{
	virtual ~AppComponent() {};

	virtual void keyCallback(int key, int scancode, int action, int mods) {}
	virtual void mouseButtonCallback(int button, int action, int mods) {}
	virtual void scrollCallback(double xoffset, double yoffset) {}
	virtual void cursorPosCallback(double xpos, double ypos) {}
	virtual void framebufferSizeCallback(int width, int height) {}
	virtual void onFrameBeginCallback() {}
	virtual void onRender(const RenderContext *ctx) {}
};

struct MyApp 
{
	int width;
	int height;
	float aspect;

	std::vector<std::weak_ptr<AppComponent>> components;

	//-----------------------------------------------------------------------------
	
	static std::unique_ptr<MyApp> create(GLFWwindow *window);
	~MyApp();

	template<typename T>
	int addComponent(std::shared_ptr<T> t_component); 

	static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
	static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
	static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
	static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
	static void framebufferSizeCallback(GLFWwindow* window, int width, int height);

	void onFrameBeginCallback(GLFWwindow* window);
	void onFrameEndCallback(GLFWwindow* window);

	void renderComponents(const RenderContext *ctx);
private:
	MyApp() {}
};

extern int init_gl_basic(GLFWwindow *window);

//--------------------------------------------------------------------------------------------------
// Template defintions

template<typename T>
int MyApp::addComponent(std::shared_ptr<T> t_component) 
{
	components.push_back(t_component);
	return 0;
}

#endif

