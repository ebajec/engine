#ifndef SIMULATION_H
#define SIMULATION_H

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <memory>

#include "hopf.h"
#include "ui.h"
#include "window.h"
#include "shader.h"

#define UI_PARAMS_PANEL_WIDTH 600
#define UI_PARAMS_PANEL_HEIGHT 200


/**********************************************************************************
 * 
 * Main window/program.  Renders hopf fibration and includes UI to control
 * some parameters.
 * 
 **********************************************************************************/

class HopfSimulation : public BaseViewWindow {
public:
    HopfSimulation(const char* title, int width, int height, int x, int y);

protected:
    void windowLoop();

    bool initFiberData();
    bool initShaders();

    void updatePositions();
    void updateSimulation();

    void renderUI();

    static void controlViewportRenderCallback(void* usr, Camera& camera);
    
    static void sceneViewportRenderCallback(void* usr, Camera& camera);
    /**
     * Called after creating the control viewport with ImGui.  Allows custom behaviour to be specified.
     */
    static void controlViewportBehaviorCallback(void* usr, Viewport* viewport);
    /**
     * Called after creating the scene viewport with ImGui.  Allows custom behaviour to be specified.
     */
    static void sceneViewportBehaviorCallback(void* usr, Viewport* viewport);

    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);

    std::shared_ptr<ShaderManager> shaderManager;
    std::shared_ptr<SimulationParams> params;

    std::unique_ptr<CameraUpdater> m_cameraUpdater;

    HopfFibrationDisplay  m_hopfDisplay;
    SphereController      m_controller;

    ImGuiContextGLFW ui;

    Viewport m_controlViewport, m_sceneViewport;
};






#endif