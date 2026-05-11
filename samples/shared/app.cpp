#include "app.h"

#include "ev2/utils/log.h"

#include "ev2/device.h"
#include "ev2/render.h"

// imgui
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include "backends/imgui_impl_opengl3.h"

// STL / libc
#include <cstdio>
#include <csignal>
#include <atomic>

#ifdef __linux__

// posix
#include <unistd.h>

static std::atomic_int g_should_close = false;;

void handle_sigint(int sig)
{
	(void)sig;
	g_should_close = true;
	log_info("received SIGINT");
}
#endif

#ifdef EV2_BACKEND_OPENGL
int init_gl_context(GLFWwindow *window)
{
	glfwMakeContextCurrent(window);

	#ifndef NDEBUG
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
	#endif

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fprintf(stderr,"Failed to initialize GLAD\n");
		return EXIT_FAILURE;
    }

	return EXIT_SUCCESS;
}
#endif

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

App::App(int width, int height, const char *title)
{
	win.width = width;
	win.height = height;
	win.title = title;
}

void App::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));

	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) 
	{
		if (app->input.mouse_mode == GLFW_CURSOR_DISABLED) {
			app->input.mouse_mode = GLFW_CURSOR_NORMAL;
		} else  {
			app->input.mouse_mode = GLFW_CURSOR_DISABLED;
		}

		glfwSetInputMode(window, GLFW_CURSOR, app->input.mouse_mode);
	}

	for (const key_callback_t callback : app->key_callbacks) {
		callback(key, scancode, action, mods);
	}
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

	app->input.scroll.x += xoffset;
	app->input.scroll.y += yoffset;

	app->input.scroll_delta.x = xoffset;
	app->input.scroll_delta.y = yoffset;
}
void App::cursor_pos_callback(GLFWwindow* window, double xpos, double ypos)
{
	ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));

	glm::dvec2 pos = glm::dvec2(xpos, ypos);
}
void App::framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	App *app = static_cast<App*>(glfwGetWindowUserPointer(window));

	if (width != app->win.width || height != app->win.height) {
		app->win.width = width;
		app->win.height = height;

		app->input.needs_resize = true;
	}
}

void App::update_input()
{
	std::swap(input.mouse_pos[0], input.mouse_pos[1]);
	glfwGetCursorPos(win.ptr, &input.mouse_pos[0].x, &input.mouse_pos[0].y);

	ImGuiIO& io = ImGui::GetIO();
	input.mouse_in_gui = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || io.WantCaptureMouse;
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
	if (g_should_close) {
		glfwSetWindowShouldClose(win.ptr, true);
		return SHOULD_CLOSE;
	}

	int result = OK;

	// I find it nicer to have ImGui captured wherever I need it to between frames
	if (frame_counter++) {
	} else {
		input.t0 = glfwGetTime();
	}

	input.t1 = input.t0;
	input.t0 = glfwGetTime();
	input.dt = input.t0 - input.t1;
	input.scroll_delta = glm::vec2(0);

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	glfwPollEvents();

	update_input();

	if (input.needs_resize) {
		if ((result = resize(win.width, win.height)) != App::OK)
			return result;

		input.needs_resize = false;
	}

	imgui();

	return result;
}

void App::begin_frame()
{
	ev2::begin_frame(dev);
}

void App::end_frame()
{
	ev2::end_frame(dev);

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());	
	ImGui::EndFrame();

	glfwSwapBuffers(win.ptr);
}

int App::initialize(int argc, char *argv[])
{
	for (int i = 0; i < argc; ++i) {
		if (!strcmp(argv[i],"--wayland"))
			glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
		else if (!strcmp(argv[i],"--x11"))
			glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
	}

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

	ImPlot::CreateContext();

	glfwSetKeyCallback(win.ptr,&App::key_callback);
	glfwSetMouseButtonCallback(win.ptr,&App::mouse_button_callback);
	glfwSetScrollCallback(win.ptr,&App::scroll_callback);
	glfwSetCursorPosCallback(win.ptr,&App::cursor_pos_callback);
	glfwSetFramebufferSizeCallback(win.ptr, &App::framebuffer_size_callback);
	glfwSetWindowUserPointer(win.ptr, this);

	dev = ev2::create_device(RESOURCE_PATH);

	std::signal(SIGINT, handle_sigint);

	if (!dev)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

void App::terminate()
{
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	ImPlot::DestroyContext();

	ev2::destroy_device(dev);

	glfwDestroyWindow(win.ptr);
	glfwTerminate();
}

void App::imgui()
{
	ImGuiViewport* vp = ImGui::GetMainViewport();
	ImVec2 work_pos  = vp->WorkPos;
	ImVec2 work_size = vp->WorkSize;

	float panel_w = 200.f;

	ImGui::SetNextWindowPos(ImVec2(0,0),ImGuiCond_Always);
	ImGui::SetNextWindowPos(work_pos);
	ImGui::SetNextWindowSize(ImVec2(panel_w, work_size.y));

	ImGui::Begin("Editor"); 

	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::CollapsingHeader("Frame times")) {
		plot_frame_times(input.dt);
	}
	ImGui::End();
}

