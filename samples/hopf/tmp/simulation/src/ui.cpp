#include "ui.h"
#include "camera.h"
#include "defines.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <mutex>
#include <time.h>

ImGuiContextGLFW::ImGuiContextGLFW(GLFWwindow * window)
{
    this->window = window;
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    this->ptr = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplGlfw_InitForOpenGL(this->window, true);
    ImGui_ImplOpenGL3_Init("#version 430");
}

ImGuiContextGLFW::~ImGuiContextGLFW()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

bool ImGuiElement::add(ImGuiContextGLFW* ctx)
{
    if (!ctx) return false;
    this->id = ImGui::GetID(name);
    this->ctx = ctx;
    return true;
} 

Viewport::Viewport() :
    pos(0,0),
    size(100,100),
    camera(vec3(1,0,0),vec3(0,0,0)),
    m_renderCallback(nullptr),
    m_behaviorCallback(nullptr)
{

}

Viewport::Viewport(int width, int height, ivec2 pos, Camera camera) :
    pos(pos),
    size(ivec2(width,height)),
    camera(camera),
    m_renderCallback(nullptr),
    m_behaviorCallback(nullptr)
{
    camera.resize(size.x, size.y);
}

struct ScreenArea
{
    int x,y,width,height;
};

static ScreenArea convertToGLScreenCoords(ivec2 pos, ivec2 size, int winHeight, int winWidth)
{
    return {pos.x,winHeight-size.y -pos.y,size.x, size.y - 20};
}

bool Viewport::add(ImGuiContextGLFW * ctx)
{
    if (!ImGuiElement::add(ctx)) return false;

    if (isFixedPos)  ImGui::SetNextWindowPos(ImVec2(pos.x,pos.y));
    if (isFixedSize) ImGui::SetNextWindowSize(ImVec2(size.x,size.y));

    int flags = 
    (isFixedPos  ? ImGuiWindowFlags_NoMove : 0) | 
    (isFixedSize ? ImGuiWindowFlags_NoResize : 0) | 
    ImGuiWindowFlags_NoCollapse
    ;
    
    if (ImGui::Begin(name, nullptr,flags))
    {
        pos = fromImVec2(ImGui::GetWindowPos());
        size = fromImVec2(ImGui::GetWindowSize());
        m_isCollapsed = false;

        if (m_behaviorCallback) m_behaviorCallback(m_usrBehavior,this);
    }     
    else 
    {
        m_isCollapsed = true;
    }                  
	ImGui::End();

    return true;
}

void Viewport::attachCameraUpdater(CameraUpdater* camUpdater)
{
    Camera* cameraView  = &(this->camera);
    Camera* cameraActive = camUpdater->getCamera();

    if (ImGui::IsWindowFocused()) 
    {     
        camUpdater->setData(this->ctx->window,cameraView,this->pos,this->size);
    }
    else if (cameraActive == cameraView)    
    {
        camUpdater->setData(nullptr,nullptr);
	}
}

void Viewport::render()
{
    int winWidth, winHeight;
    glfwGetWindowSize(ctx->window, &winWidth, &winHeight);

    camera.resize(size.x, size.y);
    camera.updateUbo();

    if (!m_isCollapsed && m_renderCallback)
    {
        ScreenArea glCoords = convertToGLScreenCoords(pos, size, winHeight, winWidth);
        glViewport(glCoords.x,glCoords.y,glCoords.width,glCoords.height);
        m_renderCallback(m_usrRender, camera);
    }       
}

ivec2 Viewport::getMousePos(ivec2 mousePosAbs)
{
    mat2 flipy = {1,0,0,-1};
    ivec2 center = pos + size/2;
    return flipy*(ivec2(mousePosAbs) - center);
}

ivec2 Viewport::getCenterAbs()
{
    return pos + size/2;
}

void Viewport::setRenderCallback(void* usr, void (*renderCallback)(void* usr, Camera& camera))
{
    m_usrRender = usr;
    m_renderCallback = renderCallback;
}

void Viewport::setBehaviorCallback(void* usr, void (*behaviorCallback)(void* usr, Viewport* viewport))
{
    m_usrBehavior = usr;
    m_behaviorCallback = behaviorCallback;
}

void Viewport::fixPos(ivec2 pos)
{
    this->pos = pos;
    isFixedPos = true;
}

void Viewport::fixSize(ivec2 size)
{
    this->size = size;
    isFixedSize = true;
}

CameraUpdater::CameraUpdater():
    m_camera(nullptr),
    m_running(true),
    m_sensitivity(0),
    m_speed(0),
    direction(0,0,0),
    angle(0,0)
{
    m_thread = std::thread(&CameraUpdater::run,this);
}

CameraUpdater::CameraUpdater(Camera* camera, float sensitivity, float speed):
    m_camera(camera),
    m_running(true),
    m_sensitivity(sensitivity),
    m_speed(speed),
    direction(0,0,0),
    angle(0,0)
{
    m_thread = std::thread(&CameraUpdater::run,this);
}

CameraUpdater::~CameraUpdater()
{
    m_running = false;

    if (!m_running && m_thread.joinable()) 
        m_thread.join();
}

void CameraUpdater::rotate(float xpos, float ypos)
{
    if (!m_camera) return;
    
    vec2 mid = m_viewCenter;

    float dx = xpos - mid.x;
    float dy = ypos - mid.y;

    dx *= -m_sensitivity;
    dy *= -m_sensitivity;
    angle = vec2(dx,dy);

    m_camera->rotate(angle.y, angle.x);
    printf("%f,%f\n",angle.x,angle.y);
    if (m_window) glfwSetCursorPos(m_window,m_viewCenter.x,m_viewCenter.y);
}

void CameraUpdater::setData(GLFWwindow* window, Camera* camera,ivec2 viewPos, ivec2 viewSize)
{
    if (camera == m_camera) return;

    std::lock_guard<std::mutex> lock(m_mtx);

    if (!window && m_window)
    {
        disableMouseControls(m_window);
    }    

    m_viewCenter = viewPos + viewSize/2;
    m_camera = camera;
    m_window = window;

    if (m_window) 
    {
        enableMouseControls(m_window);
        glfwSetCursorPos(m_window,m_viewCenter.x,m_viewCenter.y);
    }
}

void CameraUpdater::run()
{
    struct timespec req;

    // Set the desired sleep time (1.5 seconds)
    req.tv_sec = 0;    // 1 second
    req.tv_nsec = 1000;

    while (m_running)
    {
        std::unique_lock<std::mutex> lock(m_mtx);
        if (m_camera)
        {   
            m_camera->translate(direction, m_speed*1e-6);
            
        }   
        lock.unlock();
        nanosleep(&req, NULL);
    }
}

void enableMouseControls(GLFWwindow* window)
{
	//Center cursor so camera does not jerk
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	if (glfwRawMouseMotionSupported()) {
		glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
	}
}

void disableMouseControls(GLFWwindow* window)
{
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	if (glfwRawMouseMotionSupported()) {
		glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
	}
}

void updateMotionWASD(vec3& motionDir, bool firstInput)
{   
    if (ImGui::IsKeyPressed(ImGuiKey_W,false))         motionDir += vec3(0,0,1);
    if (ImGui::IsKeyPressed(ImGuiKey_A,false))         motionDir += vec3(-1,0,0);
    if (ImGui::IsKeyPressed(ImGuiKey_S,false))         motionDir += vec3(0,0,-1);
    if (ImGui::IsKeyPressed(ImGuiKey_D,false))         motionDir += vec3(1,0,0);
    if (ImGui::IsKeyPressed(ImGuiKey_Space,false))     motionDir += vec3(0,1,0);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftShift,false)) motionDir += vec3(0,-1,0);

    if (!firstInput && ImGui::IsKeyReleased(ImGuiKey_W))              motionDir -= vec3(0,0,1);
    if (!firstInput && ImGui::IsKeyReleased(ImGuiKey_A))              motionDir -= vec3(-1,0,0);
    if (!firstInput && ImGui::IsKeyReleased(ImGuiKey_S))              motionDir -= vec3(0,0,-1);
    if (!firstInput && ImGui::IsKeyReleased(ImGuiKey_D))              motionDir -= vec3(1,0,0);
    if (!firstInput && ImGui::IsKeyReleased(ImGuiKey_Space))          motionDir -= vec3(0,1,0);
    if (!firstInput && ImGui::IsKeyReleased(ImGuiKey_LeftShift))      motionDir -= vec3(0,-1,0);
}

