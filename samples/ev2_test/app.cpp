#include "app.h"

#include "engine/utils/log.h"

#include "ev2/device.h"
#include "ev2/render.h"

// imgui
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include "backends/imgui_impl_opengl3.h"

// STL / libc
#include <cstdio>

extern int init_gl_context(GLFWwindow *window);

void print_glfw_platform()
{
	int p = glfwGetPlatform(); 

	const char *s;
	switch (p) {
		case GLFW_PLATFORM_X11: s = "X11"; break;
		case GLFW_PLATFORM_WAYLAND: s = "Wayland"; break;
		case GLFW_PLATFORM_WIN32: s = "Win32"; break;
	}
	printf("\x1b[32mGLFW is running on %s\x1b[0m\n", s); 
}

void App::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));
}
void App::mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
	ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));

	if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_PRESS) {
		app->input.left_mouse_pressed = true;
	}
	if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_RELEASE) {
		app->input.left_mouse_pressed = false;
	}
	if (button == GLFW_MOUSE_BUTTON_2 && action == GLFW_PRESS) {
		app->input.right_mouse_pressed = true;
	}
	if (button == GLFW_MOUSE_BUTTON_2 && action == GLFW_RELEASE) {
		app->input.right_mouse_pressed = false;
	}

}
void App::scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));
}
void App::cursor_pos_callback(GLFWwindow* window, double xpos, double ypos)
{
	ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));

	glm::dvec2 pos = glm::dvec2(xpos, ypos);

	//std::swap(app->input.mouse_pos[0], app->input.mouse_pos[1]);
	//app->input.mouse_pos[0] = pos;
}
void App::framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));

	if (width != app->win.width || height != app->win.height) {
		if (app->resize(width, height) == App::OK) {
			app->win.width = width;
			app->win.height = height;
		}
	}
}

void App::update_input()
{
	std::swap(input.mouse_pos[0], input.mouse_pos[1]);
	glfwGetCursorPos(win.ptr, &input.mouse_pos[0].x, &input.mouse_pos[0].y);
}

int App::resize(int width, int height)
{
	glfwSetWindowSize(win.ptr, width, height);

	win.width = width;
	win.height = height;

	int result = OK;

	return result;
}

int App::update()
{
	if (glfwWindowShouldClose(win.ptr))
		return SHOULD_CLOSE;

	int result = OK;

	if (frame_counter++) {
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());	
		ImGui::EndFrame();

		ev2::end_frame(dev);

		glfwSwapBuffers(win.ptr);
	}

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	ImGuiIO& io = ImGui::GetIO();
	input.mouse_in_gui = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || io.WantCaptureMouse;

	glfwPollEvents();
	update_input();

	ev2::begin_frame(dev);
	return result;
}

int App::initialize()
{
	glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
	if (!glfwInit()) {
		log_error("Failed to initialize GLFW!");
		return EXIT_FAILURE;
	}
	print_glfw_platform();

	win.ptr = glfwCreateWindow(win.width, win.height, win.title, nullptr, nullptr);

	if (!win.ptr) {
		log_error("Failed to create GLFW window");
		return EXIT_FAILURE;
	}

	if (init_gl_context(win.ptr) < 0) {
		log_error("Failed to initialize OpenGL");
		return EXIT_FAILURE;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable keyboard controls
	io.FontGlobalScale = 1.0f; 

	// Set ImGui style
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(win.ptr, true);
	ImGui_ImplOpenGL3_Init("#version 450");

	glfwSetKeyCallback(win.ptr,&App::key_callback);
	glfwSetMouseButtonCallback(win.ptr,&App::mouse_button_callback);
	glfwSetScrollCallback(win.ptr,&App::scroll_callback);
	glfwSetCursorPosCallback(win.ptr,&App::cursor_pos_callback);
	glfwSetFramebufferSizeCallback(win.ptr, &App::framebuffer_size_callback);
	glfwSetWindowUserPointer(win.ptr, this);

	dev = ev2::create_device(RESOURCE_PATH);

	if (!dev)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

void App::terminate()
{
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	ev2::destroy_device(dev);

	glfwDestroyWindow(win.ptr);
	glfwTerminate();
}

