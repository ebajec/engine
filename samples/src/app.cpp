#include "app.h"

// imgui
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include "backends/imgui_impl_opengl3.h"

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

void MyApp::onFrameUpdateCallback(GLFWwindow *window) 
{
	static double t0 = -1; 

	if (t0 < 0) {
		g_.dt = 1.0/60.0;
	} else {
		g_.dt = glfwGetTime() - t0;
	}

	t0 = glfwGetTime();

	for (auto &ptr : components) {
		if (auto shared = ptr.lock(); shared) 
			shared->onFrameUpdateCallback();
	}
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

int glfw_init_gl_basic(GLFWwindow *window)
{
	glfwMakeContextCurrent(window);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fprintf(stderr,"Failed to initialize GLAD\n");
		return EXIT_FAILURE;
    }

	std::string extensions ((const char *)glGetString(GL_EXTENSIONS));

	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);       // makes callback synchronous

	glDebugMessageCallback([]( GLenum source,
							  GLenum type,
							  GLuint id,
							  GLenum severity,
							  GLsizei length,
							  const GLchar* message,
							  const void* userParam )
	{
		fprintf(stderr,
		  "GL DEBUG: [%u] %s\n"
		  "    Source:   0x%x\n"
		  "    Type:     0x%x\n"
		  "    Severity: 0x%x\n"
		  "    Message:  %s\n\n",
		  id, (type == GL_DEBUG_TYPE_ERROR ? "** ERROR **" : ""),
		  source, type, severity, message);

	}, nullptr);

	glDebugMessageControl(
		GL_DONT_CARE,          // source
		GL_DONT_CARE,          // type
		GL_DONT_CARE,          // severity
		0, nullptr,            // count + list of IDs
		GL_TRUE);              // enable

    glfwWindowHint(GLFW_SAMPLES, 4);
    glEnable(GL_MULTISAMPLE);

	const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* vendor = glGetString(GL_VENDOR);
    const GLubyte* glslVersion = glGetString(GL_SHADING_LANGUAGE_VERSION);

    printf("Renderer: %s\n", renderer);
    printf("OpenGL version: %s\n", version);
    printf("Vendor: %s\n", vendor);
    printf("GLSL version: %s\n", glslVersion);
	return EXIT_SUCCESS;
}
