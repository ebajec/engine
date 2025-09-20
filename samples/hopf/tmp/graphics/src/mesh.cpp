#include "mesh.h"
#include "glm/ext/scalar_constants.hpp"
    
void surfaceParamMeshIndex(int uCount, int vCount, int i, int j, int b1, int b2, size_t& counter, std::vector<uint>& indices)
{
    if (b1 == 0 && b2 == 0)
    {
        int iNext = i+1;
        int jNext = j+1;

        if (iNext == uCount || jNext == vCount) return;

        indices[counter++] = i*vCount + j;
        indices[counter++] = i*vCount + jNext;
        indices[counter++] = iNext*vCount + jNext;

        indices[counter++] = i*vCount + j;
        indices[counter++] = iNext*vCount + jNext;
        indices[counter++] = iNext*vCount + j;
        return;
    }
    if (b1 == 1 && b2 == 0)
    {
        int iNext = i+1;
        int jNext = (j >= uCount - 1) ? 0 : j+1;

        if (iNext == uCount) return;

        indices[counter++] = i*vCount + j;
        indices[counter++] = i*vCount + jNext;
        indices[counter++] = iNext*vCount + jNext;

        indices[counter++] = i*vCount + j;
        indices[counter++] = iNext*vCount + jNext;
        indices[counter++] = iNext*vCount + j;
    }
    if (b1 == 0 && b2 == 1)
    {
        return;
    }
    if (b1 == 2 && b2 == 1)
    {
        int iNext = (i >= uCount - 1) ? 0 : i+1;
        int jNext = (j >= uCount - 1) ? 0 : j+1;

        indices[counter++] = i*vCount + j;
        indices[counter++] = i*vCount + jNext;
        indices[counter++] = iNext*vCount + jNext;

        indices[counter++] = i*vCount + j;
        indices[counter++] = iNext*vCount + jNext;
        indices[counter++] = iNext*vCount + j;
    return;
    }
    return;
}

std::pair<std::vector<Vertex>,std::vector<uint>> meshFromSurface(
    vec3 (*param)(float, float), const int uCount, const int vCount, const int b1, const int b2)
{
    std::vector<Vertex> points(uCount*vCount);
    std::vector<uint> indices(6*(uCount+1)*(vCount+1));
    float u,v;
    size_t indexCount = 0;

    float du = 1.0f/(float)uCount;
    float dv = 1.0f/(float)vCount;
    float eps = pow(glm::epsilon<float>(),2);

    for (int i = 0; i < uCount; i++)
    {
        for (int j = 0; j < vCount; j++)
        {
            u = i*du;
            v = j*dv;

            Vertex& vertex = points[i*uCount + j];
            vec3 p = param(u,v);
            vertex.position = vec4(p,1);
            vertex.color = vec4(0.5,0.5,0.5,0.5);
            vec3 normal = glm::cross(param(u + du,v + 2*dv) - p, param(u+2*du,v+dv) - p);

            if (glm::dot(normal,normal) > eps)
                vertex.normal = vec4(normalize(normal),0);
            
            surfaceParamMeshIndex(uCount,vCount,i,j,b1,b2,indexCount,indices);
        }
    }
    indices.shrink_to_fit();
    return {points,indices};
}

Mesh::Mesh()
{
    this->attribPointer(0, 4, GL_FLOAT, GL_FALSE, 0);
    this->attribPointer(1, 4, GL_FLOAT, GL_FALSE, (void*)sizeof(vec4));
    this->attribPointer(2, 4, GL_FLOAT, GL_FALSE, (void*)(sizeof(vec4) + sizeof(vec4)));
}


MeshGen::MeshGen()
{
}

MeshGen::~MeshGen()
{
}

void MeshGen::setShaderManager(ShaderManager *shaderManager)
{
    shaders = shaderManager;
}

void MeshGen::polylineMesh(Buffer& lines, Buffer& instances, PrimitiveData<Vertex>& mesh, uint resolution)
{
    uint numLines = 1;//instances.size()/sizeof(LineInstance);
    uint totalPoints = lines.size()/sizeof(LineData);
    uint detail = resolution;

	//set up bindings on GPU
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,lines.id());
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,instances.id());
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,tempData.id());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,mesh.vbo()->id());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,4,mesh.ebo()->id());

    uint localSizeX = 32;
    uint localSizeY = 16;
    uint localSizeZ = 1;

    uint nGroupsX = totalPoints/localSizeX;
    uint nGroupsY = detail/localSizeY;
    uint nGroupsZ = numLines/localSizeZ ;

    ShaderProgram shader = shaders->program("polyline_mesh");

    shader.setUniform("numLines",numLines);
    shader.setUniform("lineDetail",detail);
    shader.use();

	glDispatchCompute(nGroupsX,nGroupsY,nGroupsZ);

	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
}
