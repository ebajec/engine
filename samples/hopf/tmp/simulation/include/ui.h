#ifndef UI_H
#define UI_H

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <thread>
#include <mutex>

#include "glm/ext/scalar_constants.hpp"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "defines.h"
#include "camera.h"
#include "imgui_internal.h"

static inline ivec2 fromImVec2(ImVec2 v) { return ivec2(v.x,v.y);}
static inline ImVec2 fromivec2(ivec2 v) { return ImVec2(v.x,v.y);}

class CameraUpdater;
class Viewport;

class ImGuiContextGLFW
{
public:
    ImGuiContextGLFW(GLFWwindow * window);
    ~ImGuiContextGLFW();

    ImGuiContext * ptr;
    GLFWwindow * window;
};

class ImGuiElement
{
public:
    virtual bool add(ImGuiContextGLFW * ctx);
    const char * name = "Debug";
    ImGuiContextGLFW* getCtx() {return ctx;}
protected:
    ImGuiContextGLFW * ctx = nullptr;
    ImGuiID id;
};

class Viewport : public ImGuiElement
{
public:
    Viewport();
    Viewport(int width, int height, ivec2 pos,Camera camera);

    bool add(ImGuiContextGLFW * ctx);
    void render();

    void attachCameraUpdater(CameraUpdater* camUpdater);

    void setRenderCallback(void* usr, void (*renderCallback)(void* usr, Camera& camera));
    void setBehaviorCallback(void* usr, void (*behaviorCallback)(void* usr, Viewport* viewport));

    /**
     * Calculate the mouse position in normal cartesian coordinates, relative 
     * to the center of the viewport. 
     */
    ivec2 getMousePos(ivec2 mousePos);
    ivec2 getCenterAbs();

    void fixPos(ivec2 pos);
    void fixSize(ivec2 size);

    /****************** PUBLIC MEMEBERS*********************/
    /* These can be modified however without casuing issues*/

    Camera camera;

    ivec2 pos;
    ivec2 size; 

    bool isFixedPos = false;
    bool isFixedSize = false;

private:
    bool m_isCollapsed = false;
    bool m_isFocused = false;

    void (*m_behaviorCallback)(void* usr, Viewport* viewport) = nullptr;
    void (*m_renderCallback)(void* usr, Camera& camera) = nullptr;

    void* m_usrRender = nullptr;
    void* m_usrBehavior = nullptr;
};

class CameraUpdater
{
public:
    CameraUpdater();
    CameraUpdater(Camera* camera, float sensitivity, float speed);
    ~CameraUpdater();

    void rotate(float dx, float dy);
    void setData(GLFWwindow* window, Camera* camera, ivec2 viewPos = ivec2(0), ivec2 viewSize = ivec2(0));
    Camera * getCamera() {return m_camera;}
    
    vec3 direction;    // Defines the current direction of motion for the camera, relative
                       // to it's orientation. 
    vec2 angle;        // Defines the pitch (x) and yaw (y) angles by which the camera should
                       // move per update.
private:
    void run();

    Camera*     m_camera;
    GLFWwindow* m_window;
    
    ivec2       m_viewCenter;

    // Manages access to camera so that switch camera does not cause 
    // data race with updater.
    std::mutex  m_mtx;
    std::thread m_thread;

    bool        m_running;
    float       m_sensitivity;
    float       m_speed; 
};

extern void enableMouseControls(GLFWwindow* window);
extern void disableMouseControls(GLFWwindow* window);
/**
 * Updates a motion direction vector based on WASD keypresses
 */
extern void updateMotionWASD(vec3& motionDir, bool firstInput = false);

#endif