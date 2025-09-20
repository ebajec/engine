#pragma once

#ifndef GLFW_WINDOW_H
#define GLFW_WINDOW_H
#include <GL/glew.h> 
#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>
#include <thread>
#include <functional>
#include <map>
#include "camera.h"
#include "ui.h"

class CamManager;
class KeyManager;
class BaseViewWindow;

struct WinState{
	enum ControlState
	{
		CONTROL_GUI,
		CONTROL_CAMERA	
	};
	bool is_running = false;
	ControlState control_state = CONTROL_GUI;
};

/*Maps or unmaps keys + actions to functions, and holds information regarding the 
* respective function for a key and action pair. 
* 
* Keys and actions must be identified with GLFW macros.  Look at GLFW docs for 
* more info.
*/
class KeyManager {
private:
	std::map<std::pair<int, int>, std::function<void()>> keymap;
public:
	KeyManager() {}
	//maps function to key action
	void mapKey(int key, int action, std::function<void()> func) { keymap.insert({ {key,action},func }); }
	void unmap(int key, int action) { keymap.erase({ key,action }); }
	//calls function for key action, if one exists  
	void callKeyFunc(int key, int action);
};

/*
* Provides a base for creating windows with GLFW.  Natively handles keyboard
* input, mouse input, and camera controls.  By default, moves camera with WASD
* and mouse.  
*/
class BaseViewWindow {

public:
	BaseViewWindow(const char* title, int width, int height, int xwin, int ywin, GLFWmonitor* monitor = NULL, GLFWwindow* share = NULL);
	~BaseViewWindow();
	bool isRunning() { return this->m_state.is_running;}
	void launch(const char* title, GLFWmonitor* monitor, GLFWwindow* share );
	void close();
	void waitForClose();

protected:
	//Intializes window, runs main loop until close, then terminates OpenGL context
	void windowProgram(
		const char* title,
		GLFWmonitor* monitor,
		GLFWwindow* share);

	//Main loop of program. 
	virtual void windowLoop() {};

	//callback for keyboard input event
	static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
	
	void enableCameraControls();
	void disableCameraControls();

	int m_height;
	int m_width;

	vec2 m_mousePos;

	GLFWwindow* m_window = NULL;
	WinState m_state;
	Camera m_camera;
	KeyManager m_keyManager;
};






#endif
