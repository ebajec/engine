#include "simulation.h"
#include "camera.h"
#include "hopf.h"
#include "imgui.h"
#include "defines.h"
#include "shader.h"
#include "simulation.h"
#include "ui.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

/**********************************************************************************
 * 
 * Main implementation
 * 
 **********************************************************************************/
HopfSimulation::HopfSimulation(const char* title, int width, int height, int x, int y) : 
    BaseViewWindow(title, width, height,x,y,NULL,NULL),
    ui(m_window),
    shaderManager(std::make_shared<ShaderManager>("../graphics/shader/")),
    params(std::make_shared<SimulationParams>()),
    m_cameraUpdater(std::make_unique<CameraUpdater>(nullptr,3e-3,400)),
    m_controller(shaderManager, params),
    m_hopfDisplay(shaderManager, params)
{   
    m_controlViewport = Viewport(
        UI_PARAMS_PANEL_WIDTH, 
        UI_PARAMS_PANEL_WIDTH, 
        ivec2(m_width - UI_PARAMS_PANEL_WIDTH,m_height - UI_PARAMS_PANEL_WIDTH),
        Camera(vec3(1,0,0),vec3(0,0,0),1920,1080,PI/4,3,20000));
    m_controlViewport.name = "S2";  

    m_controlViewport.setRenderCallback(this,HopfSimulation::controlViewportRenderCallback);    
    m_controlViewport.setBehaviorCallback(this, HopfSimulation::controlViewportBehaviorCallback);

    m_sceneViewport = Viewport(
        m_width - UI_PARAMS_PANEL_WIDTH, 
        m_height, 
        ivec2(0,0),
        Camera(vec3(1,0,0),vec3(-5,5,0),1920,1080,PI/4,0.01,20000));
    m_sceneViewport.name = "Hopf Fibration";

    m_sceneViewport.setRenderCallback(this, HopfSimulation::sceneViewportRenderCallback);
    m_sceneViewport.setBehaviorCallback(this, HopfSimulation::sceneViewportBehaviorCallback);

    // Set up simulation parameters;
    params->animSpeed = 0.0;
    params->drawLines = true;
    params->drawMesh = true;
    params->maxFibers = FIBER_COUNT;
    
    glfwSetCursorPosCallback(m_window, HopfSimulation::cursorPosCallback);

    if (!initShaders()) {
        printf("ERROR: Failed to initialize shaders.\n");
        return;
    }

    initFiberData();
    windowLoop();
}

bool HopfSimulation::initShaders() {
    // Initialize shader programs and camera
    shaderManager->addProgram(
    "default",           
    {"default.vert","solid_color.frag"});

    shaderManager->addProgram(
    "blinn-phong",       
    {"default.vert","blinnphong.frag"});

    shaderManager->addProgram(
    "blinn-phong-instanced", 
    {"spheres_instanced.vert","blinnphong.frag"});

    shaderManager->addProgram(
    "polyline_0_tangents",              
    {"polyline_0_tangents.comp"});

    shaderManager->addProgram(
    "polyline_1_normals",              
    {"polyline_1_normals.comp"});

    shaderManager->addProgram(
    "polyline_2_mesh",              
    {"polyline_2_mesh.comp"});

    shaderManager->addProgram(
    "hopf",                  
    {"hopf.comp"});

    shaderManager->addProgram(
    "spheres_transform",                
    {"spheres_transform.comp"});

    shaderManager->addProgram(
    "mesh_normals", 
    {"mesh_normals.comp"});

    shaderManager->addProgram(
    "mesh_normals_reset", 
    {"mesh_normals_reset.comp"});
    return true;
}   

float curl = 0;
bool recalculatePoints = false;

bool HopfSimulation::initFiberData() 
{
    std::vector<SpherePointData> points(FIBER_COUNT);

    auto spherePath = [](float t) 
    {
        t = 2*PI*t;
        float s = PI/4 + curl*PI/4*sin(5*t);
        return vec3(sin(s)*cos(t),sin(s)*sin(t),cos(s));
    };

    for (unsigned int i = 0; i < FIBER_COUNT; i++)
    {
        float t = (float)i/(float)FIBER_COUNT;
        points[i].position = vec4(spherePath(t),1.0f);
    }

    m_controller.uploadPointData(points.data(),points.size()*sizeof(SpherePointData));
    m_hopfDisplay.setPoints(m_controller.getPoints());
    m_hopfDisplay.updateIndexData(FIBER_COUNT, FIBER_SIZE);
    return true; 
}

void HopfSimulation::windowLoop()
{
    printf("-----------------------------\n");
    printf("Press ESC to toggle GUI access\n");
    printf("Use WASD and mouse to move\n");

    while (!glfwWindowShouldClose(m_window)) 
    {
    	glfwGetFramebufferSize(m_window, &m_width, &m_height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (recalculatePoints)
        {
            initFiberData();
            recalculatePoints = false;
        }

        updateSimulation();

        //Rendering
        renderUI();
        m_controlViewport.render();
        m_sceneViewport.render();

    	glfwSwapBuffers(m_window);    
        glfwPollEvents();
    }
}

/**********************************************************************************
 * 
 * UI stuff
 * 
 **********************************************************************************/

void HopfSimulation::updateSimulation()
{
    m_camera.updateUbo();
    m_controller.updateBallPositions();
    m_hopfDisplay.updateFiberData();
}

void HopfSimulation::sceneViewportRenderCallback(void* usr, Camera& camera)
{
    HopfSimulation* sim = static_cast<HopfSimulation*>(usr);
    sim->m_hopfDisplay.render(camera);
}

void HopfSimulation::sceneViewportBehaviorCallback(void* usr, Viewport* viewport)
{
    HopfSimulation* sim = static_cast<HopfSimulation*>(usr);
    CameraUpdater* camUpdater = sim->m_cameraUpdater.get();

    viewport->attachCameraUpdater(camUpdater);
}

void HopfSimulation::controlViewportRenderCallback(void* usr, Camera& camera)
{
    HopfSimulation* sim = static_cast<HopfSimulation*>(usr);
    sim->m_controller.render(camera);
}

void HopfSimulation::controlViewportBehaviorCallback(void* usr, Viewport* viewport)
{
    HopfSimulation* sim = static_cast<HopfSimulation*>(usr);
    CameraUpdater* camUpdater = sim->m_cameraUpdater.get();
    viewport->attachCameraUpdater(camUpdater);
}

void HopfSimulation::cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
    ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);

	auto win = static_cast<HopfSimulation*>(glfwGetWindowUserPointer(window));

	win->m_mousePos = vec2(xpos,ypos);
    win->m_cameraUpdater.get()->rotate(xpos,ypos);	
}

void HopfSimulation::renderUI()
{
    m_sceneViewport.fixPos(ivec2(0,0));
    m_sceneViewport.fixSize(ivec2(m_controlViewport.pos.x, m_height));
    m_controlViewport.fixPos(ivec2(m_width - UI_PARAMS_PANEL_WIDTH,m_height - UI_PARAMS_PANEL_WIDTH));
    m_controlViewport.fixSize(ivec2(UI_PARAMS_PANEL_WIDTH,UI_PARAMS_PANEL_WIDTH)); 

    // Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

    // Every frame, we 
    updateMotionWASD(this->m_cameraUpdater.get()->direction);

    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        ImGui::SetWindowFocus(NULL);
    }

    m_controlViewport.add(&ui);
    m_sceneViewport.add(&ui);

	// Parameters controls
    ImGui::SetNextWindowPos(ImVec2(m_width - UI_PARAMS_PANEL_WIDTH, 0));
    ImGui::SetNextWindowSize(ImVec2(UI_PARAMS_PANEL_WIDTH, m_height - UI_PARAMS_PANEL_WIDTH));

	ImGui::Begin("Parameters", nullptr, ImGuiWindowFlags_NoResize);                          
	ImGui::SliderInt("Circle count", &params->maxFibers, 0, FIBER_COUNT);
    ImGui::SliderFloat("Animation Speed",&params->animSpeed, -1, 1);
    if (ImGui::SliderFloat("Curl",&curl,0,1))
    {
        recalculatePoints = true;
    }
    if (ImGui::Button(params->drawMesh ? "Disable mesh" : "Enable mesh"))
    {
        params->drawMesh = !params->drawMesh;
    }
    if (ImGui::Button(params->drawLines ? "Disable lines" : "Enable lines"))
    {
        params->drawLines = !params->drawLines;
    }

	ImGui::End();

	// Rendering
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());	
}
