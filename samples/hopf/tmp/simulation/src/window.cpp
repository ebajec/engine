#include "window.h"
#include <thread>
#include <tuple>

BaseViewWindow::BaseViewWindow(
	const char* title, int width, int height, int xwin, int ywin, GLFWmonitor* monitor, GLFWwindow* share
) : m_width(width), m_height(height) 
{

	// Create glfw context
	m_window = glfwCreateWindow(m_width, m_height, title, monitor, share);
	if (!m_window) {
		fprintf(stderr, "ERROR: could not open window with GLFW3\n");
		glfwTerminate();
	}
	glfwMakeContextCurrent(m_window);
	glfwSetWindowPos(m_window,xwin,ywin);

	// start GLEW extension handler
	glewExperimental = GL_TRUE;
	glewInit();
	
	const GLubyte* _renderer = glGetString(GL_RENDERER);
	const GLubyte* _version = glGetString(GL_VERSION);

	printf("Renderer: %s\n", _renderer);
	printf("OpenGL version supported %s\n", _version);

	glfwSetWindowUserPointer(m_window, this);
	glfwSetKeyCallback(m_window, keyCallback);
		/************** SET UP CAMERA **************/
	m_camera = Camera(
		vec3({ -1,0,0 }),
		vec3({ 0,0,0 }),
		m_width,
		m_height,
		PI/4,
		10);

	m_state.is_running = true;
}

BaseViewWindow::~BaseViewWindow()
{
	disableCameraControls();
	// m_cameraManager.stop();
	m_state.is_running = false;
	glfwDestroyWindow(m_window);
}

void BaseViewWindow::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	auto win = static_cast<BaseViewWindow*>(glfwGetWindowUserPointer(window));

	win->m_keyManager.callKeyFunc(key, action);
}

void BaseViewWindow::enableCameraControls()
{
	if (m_state.control_state == WinState::CONTROL_CAMERA) 
		return;
	
	//Center cursor so camera does not jerk
	glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	if (glfwRawMouseMotionSupported()) {
		glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
	}
	m_state.control_state = WinState::CONTROL_CAMERA;
	return;
}

void BaseViewWindow::disableCameraControls()
{
	if (m_state.control_state == WinState::CONTROL_GUI)
		return;

	glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	if (glfwRawMouseMotionSupported()) {
		glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
	}
	m_state.control_state = WinState::CONTROL_GUI;
	return;
}

void KeyManager::callKeyFunc(int key, int action)
{
	std::pair<int, int> operation({ key,action });

	if (keymap.contains(operation)) keymap.at(operation)();

	return;
}
