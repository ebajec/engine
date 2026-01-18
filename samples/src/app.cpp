#include "app.h"

// imgui
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include "backends/imgui_impl_opengl3.h"

#include "engine/utils/log.h"

AppGlobals g_;

void MyApp::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
	MyApp *app = static_cast<MyApp*>(glfwGetWindowUserPointer(window));

	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) 
	{
		if (g_.mouse_mode == GLFW_CURSOR_DISABLED) {
			g_.mouse_mode = GLFW_CURSOR_NORMAL;
		} else  {
			g_.mouse_mode = GLFW_CURSOR_DISABLED;
		}

		glfwSetInputMode(window, GLFW_CURSOR, g_.mouse_mode);
	}

	for (auto &ptr : app->components) {
		if (auto shared = ptr.lock(); shared) 
			shared->keyCallback(key,scancode,action,mods);
	}
}
void MyApp::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);

	if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_PRESS) {
		g_.left_mouse_pressed = true;
	}
	if (button == GLFW_MOUSE_BUTTON_1 && action == GLFW_RELEASE) {
		g_.left_mouse_pressed = false;
	}
	if (button == GLFW_MOUSE_BUTTON_2 && action == GLFW_PRESS) {
		g_.right_mouse_pressed = true;
	}
	if (button == GLFW_MOUSE_BUTTON_2 && action == GLFW_RELEASE) {
		g_.right_mouse_pressed = false;
	}

	MyApp *app = static_cast<MyApp*>(glfwGetWindowUserPointer(window));
	for (auto &ptr : app->components) {
		if (auto shared = ptr.lock(); shared) 
			shared->mouseButtonCallback(button,action,mods);
	}
}
void MyApp::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
	ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
	MyApp *app = static_cast<MyApp*>(glfwGetWindowUserPointer(window));
	for (auto &ptr : app->components) {
		if (auto shared = ptr.lock(); shared) 
			shared->scrollCallback(xoffset,yoffset);
	}
}
void MyApp::cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
	ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);

	MyApp *app = static_cast<MyApp*>(glfwGetWindowUserPointer(window));
	for (auto &ptr : app->components) {
		if (auto shared = ptr.lock(); shared) 
			shared->cursorPosCallback(xpos,ypos);
	}
}
void MyApp::framebufferSizeCallback(GLFWwindow* window, int width, int height)
{
	MyApp *app = static_cast<MyApp*>(glfwGetWindowUserPointer(window));

	app->width = width;
	app->height = height;
	app->aspect = (float)height/(float)width;

	for (auto &ptr : app->components) {
		if (auto shared = ptr.lock(); shared) 
			shared->framebufferSizeCallback(width,height);
	}
}

void MyApp::onFrameBeginCallback(GLFWwindow *window) 
{
	glfwGetFramebufferSize(window,&width,&height);

	g_.t0 = g_.t1;
	g_.t1 = glfwGetTime();

	g_.dt = std::max(g_.t1 - g_.t0,1.0/1024);

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	ImGuiIO& io = ImGui::GetIO();
	g_.mouse_in_gui = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) || io.WantCaptureMouse;

	for (auto &ptr : components) {
		if (auto shared = ptr.lock(); shared) 
			shared->onFrameBeginCallback();
	}
}

void MyApp::onFrameEndCallback(GLFWwindow* window)
{
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());	
	ImGui::EndFrame();
}

void MyApp::renderComponents(const RenderContext *ctx)
{
	for (auto &ptr : components) {
		if (auto shared = ptr.lock(); shared) 
			shared->onRender(ctx);
	}
}

std::unique_ptr<MyApp> MyApp::create(GLFWwindow *window)
{
	if (!window) return nullptr;

	std::unique_ptr<MyApp> app = std::unique_ptr<MyApp>(new MyApp());

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable keyboard controls
	io.FontGlobalScale = 1.0f; 

	// Set ImGui style
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 450");

	glfwGetWindowSize(window, &app->width, &app->height);
	app->aspect = (float)app->width/(float)app->height;

	glfwSetKeyCallback(window,&MyApp::keyCallback);
	glfwSetMouseButtonCallback(window,&MyApp::mouseButtonCallback);
	glfwSetScrollCallback(window,&MyApp::scrollCallback);
	glfwSetCursorPosCallback(window,&MyApp::cursorPosCallback);
	glfwSetFramebufferSizeCallback(window, &MyApp::framebufferSizeCallback);

	glfwSetWindowUserPointer(window, app.get());

	return app;
}

MyApp::~MyApp()
{
}

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
