#ifndef HOPF_H
#define HOPF_H

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <memory>

#include "mesh.h"
#include "renderer.h"
#include "defines.h"
#include "shader.h"


/**********************************************************************************
 * 
 * Control sphere manager
 * 
 **********************************************************************************/

struct SpherePointData
{
    vec4 position;
    vec4 color;
};

struct SimulationParams
{
    float animSpeed;
    float tOffset;
    int   maxFibers;
    int   lineDetail = 4;
    bool  drawMesh;
    bool  drawLines;
};
class SphereController
{
public:
    SphereController(
        std::shared_ptr<ShaderManager>& shaderManager, 
        std::shared_ptr<SimulationParams>& params);

    void updateBallPositions();
    void render(Camera& camera);
    void transform(mat4 trans);
    const Buffer& getPoints() {return m_points;}
    void uploadPointData(void* data, size_t size);

private:
    Buffer m_points;
    Mesh m_sphereMesh;
    Camera m_camera;
    mat4 m_geometry = mat4(1.0f);

    int pointCount;

    std::shared_ptr<SimulationParams> m_params;
    std::shared_ptr<ShaderManager> m_shaderManager;
};
class HopfFibrationDisplay
{
public:
    HopfFibrationDisplay(
        std::shared_ptr<ShaderManager>& shaderManager, 
        std::shared_ptr<SimulationParams>& params);

    void updateIndexData(const uint fiberCount, const uint fiberRes);
    void updateFiberData();
    void render(Camera& camera);

    void setPoints(const Buffer& points);
private:
    const Buffer* spherePoints;
    Buffer lineInstances;
    Buffer frameData;
    PrimitiveData<Vertex> circleData;
    PrimitiveData<Vertex> lineMeshData;

    std::shared_ptr<SimulationParams> m_params;
    std::shared_ptr<ShaderManager> m_shaderManager;
};

#endif
