//#include <GL/glext.h>

#include "hopf.h"

#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>

#include "misc.h"
#include "defines.h"
#include "mesh.h"
#include "renderer.h"
#include "shader.h"


/**********************************************************************************
 * 
 * Implementation details for SphereController
 * 
 **********************************************************************************/
SphereController::SphereController(
    std::shared_ptr<ShaderManager>& shaderManager, std::shared_ptr<SimulationParams>& params): 
    m_params(params),
    m_shaderManager(shaderManager)
{
    auto sphereParam = [](float u, float v)
    {
        return S2(2*PI*v,PI*u);
    };

    auto meshData = meshFromSurface(sphereParam,64,64,1,0);
    m_sphereMesh.uploadData(meshData.first,meshData.second,GL_STREAM_DRAW);
}

void SphereController::render(Camera& camera)
{   
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);

    ShaderProgram shader = m_shaderManager->program("blinn-phong");
    ShaderProgram instanceShader= m_shaderManager->program("blinn-phong-instanced");

    size_t indexCount = m_sphereMesh.indexCount();

    camera.bindUbo(0);

    m_sphereMesh.bindArray();
    // Render the big sphere
    shader.use();
    shader.setUniform("model",m_geometry,GL_TRUE);
    shader.setUniform("scale",1.0f);

    // Render the little balls
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,m_points.id());
    instanceShader.use();
    instanceShader.setUniform("model",m_geometry,true);
    instanceShader.setUniform("scale",0.04f);
    glDrawElementsInstanced(GL_TRIANGLES,indexCount,GL_UNSIGNED_INT,(void*)0,m_params->maxFibers);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,0);

    m_sphereMesh.unbindArray();
}
void SphereController::transform(mat4 trans) 
{
    m_geometry = m_geometry*trans;
}

void SphereController::updateBallPositions()
{
    ShaderProgram computePositions = m_shaderManager->program("spheres_transform");

    uint sphereCount = (uint)(m_points.size()/sizeof(SpherePointData));

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,m_points.id());
    computePositions.use();
    computePositions.setUniform("count",sphereCount);
    computePositions.setUniform("u_animSpeed",m_params->animSpeed);
    computePositions.dispatchCompute(sphereCount, 1, 1);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,0);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

void SphereController::uploadPointData(void* data, size_t size)
{
    this->m_points.uploadData(data,size,GL_STREAM_DRAW);
}

/**********************************************************************************
 * 
 * Implementation details for HopfHController.
 * 
 **********************************************************************************/

 HopfFibrationDisplay::HopfFibrationDisplay(
    std::shared_ptr<ShaderManager>& shaderManager, std::shared_ptr<SimulationParams>& params) :
 m_shaderManager(shaderManager), m_params(params)
 {
    circleData.reserveAttribs(FIBER_COUNT*FIBER_SIZE);
    circleData.reserveIndices(FIBER_COUNT*FIBER_SIZE*6);
    circleData.attribPointer(0, 4, GL_FLOAT, GL_FALSE, 0);
    circleData.attribPointer(1, 4, GL_FLOAT, GL_FALSE, (void*)sizeof(vec4));  
    circleData.attribPointer(2, 4, GL_FLOAT, GL_FALSE, (void*)(2*sizeof(vec4)));  

    updateIndexData(0, 0);

    lineMeshData.reserveAttribs(FIBER_COUNT*FIBER_SIZE*m_params->lineDetail);
    lineMeshData.reserveIndices(6*m_params->lineDetail*FIBER_COUNT*FIBER_SIZE);

    lineMeshData.attribPointer(0,4,GL_FLOAT,GL_FALSE,0);
    lineMeshData.attribPointer(1,4,GL_FLOAT,GL_FALSE,(void*)sizeof(vec4));
    lineMeshData.attribPointer(2,4,GL_FLOAT,GL_FALSE,(void*)((2*sizeof(vec4))));

    frameData.reserve(FIBER_COUNT*FIBER_SIZE*sizeof(TangentFrame));
 }

 void HopfFibrationDisplay::updateIndexData(const uint fiberCount, const uint fiberRes)
 {
    std::vector<InstanceLine> instances(fiberCount);
    for (unsigned int i = 0; i < fiberCount; i++)
    {
        instances[i] = 
        {
            .cmd = {
            .count = fiberRes,
            .instanceCount = 1,
            .first = i*fiberRes,
            .baseInstance = 0
            },
            .width = 0.02f,
            .avgLength = 0
        };
    }

    lineInstances.uploadData(instances);
 }

 void pipeline_polyline_mesh_compute(
    GLuint in_lineData, 
    GLuint in_instances, 
    GLuint in_tangentFrameData, 
    GLuint out_meshData, 
    GLuint out_meshIndices,

    uint numLines,
    uint totalCount,
    uint detail,

    ShaderManager* shaderManager
    )
{
    ShaderProgram polylineTangets = shaderManager->program("polyline_0_tangents");
    ShaderProgram polylineNormals = shaderManager->program("polyline_1_normals");
    ShaderProgram polylineMesh = shaderManager->program("polyline_2_mesh");

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,in_lineData);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,in_instances);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,in_tangentFrameData);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,out_meshData);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,4,out_meshIndices);

    polylineTangets.use();
    polylineTangets.setUniform("numLines",(uint)numLines);
    polylineTangets.dispatchCompute(FIBER_SIZE, 1, numLines);

    polylineNormals.use();
    polylineNormals.setUniform("numLines",(uint)numLines);
    polylineNormals.dispatchCompute(1, 1, numLines);

    polylineMesh.use();
    polylineMesh.setUniform("numLines",(uint)numLines);
    polylineMesh.setUniform("lineDetail",(uint)detail);
    polylineMesh.dispatchCompute(FIBER_SIZE,detail,FIBER_COUNT);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,4,0);
}

 void HopfFibrationDisplay::updateFiberData()
 {
    ShaderProgram hopf_map = m_shaderManager->program("hopf");
    
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,spherePoints->id());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,lineInstances.id());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,circleData.vbo()->id());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,circleData.ebo()->id());

    hopf_map.use();
    hopf_map.setUniform("numFibers",(unsigned int)FIBER_COUNT);
    hopf_map.setUniform("tOffset",(float)m_params->tOffset);
    hopf_map.dispatchCompute(FIBER_COUNT, FIBER_SIZE, 1);

	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,0);

    ShaderProgram compute_normals = m_shaderManager->program("mesh_normals");

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,circleData.vbo()->id());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,circleData.ebo()->id());

    uint numTriangles = circleData.ebo()->size()/(3*sizeof(uint));

    compute_normals.use();
    compute_normals.setUniform("numTriangles",numTriangles);
    compute_normals.dispatchCompute(numTriangles, 1, 1);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,0);

    // Generate meshes for the big circles

    pipeline_polyline_mesh_compute(
        circleData.vbo()->id(),
        lineInstances.id(),
        frameData.id(),
        lineMeshData.vbo()->id(),
        lineMeshData.ebo()->id(),

        FIBER_COUNT,
        FIBER_SIZE,
        m_params->lineDetail,
        
        m_shaderManager.get()
    );
}

void HopfFibrationDisplay::render(Camera& camera)
{
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    ShaderProgram shader = m_shaderManager->program("default");

    camera.bindUbo(0);

    shader.use();
    shader.setUniform("model",mat4(1.0f),GL_FALSE);
    shader.setUniform("scale",1.0f);
    shader.setUniform("t",(float)glfwGetTime());
    
    if (m_params->drawMesh)
    {
        circleData.bindArray();
        glDrawElements(GL_TRIANGLES, 6*m_params->maxFibers*FIBER_SIZE, GL_UNSIGNED_INT, 0);
        circleData.unbindArray();
    }

    if (m_params->drawLines)
    {
        lineMeshData.bindArray();
        glDrawElements(GL_TRIANGLES,6*m_params->lineDetail*FIBER_SIZE*m_params->maxFibers,GL_UNSIGNED_INT,0);
        lineMeshData.unbindArray();
    }
    
    glBindBufferBase(GL_UNIFORM_BUFFER,0,0);

    ShaderProgram reset_normals = m_shaderManager->program("mesh_normals_reset");

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,circleData.vbo()->id());

    reset_normals.use();
    reset_normals.setUniform("count",FIBER_COUNT*FIBER_SIZE);
    reset_normals.dispatchCompute(FIBER_COUNT*FIBER_SIZE, 1, 1);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,0);
}

void HopfFibrationDisplay::setPoints(const Buffer& points)
{
    this->spherePoints = &points;
}
