#ifndef RENDERER_LINE_H
#define RENDERER_LINE_H

#include "renderer.h"
#include "shader.h"

struct LineData 
{
    vec4 position;
    vec4 color;
};

struct TangentFrame
{
    vec4 T; 
    vec4 N;
    vec4 B;
};

struct Vertex
{
    vec4 position;
    vec4 color;
    vec4 normal;
};

struct InstanceLine
{
    DrawArraysIndirectCommand cmd;
    float width;
    float avgLength;
};

/**
* Sample points from the parameterization of a surface.  Also returns indices
* for a mesh created from the points. The topology can be specified by setting the
* first and second betti numbers. 
*  
* @param param - Parameterization function to be evaluated.
* @param uCount - Number of samples in first coordinate.
* @param vCount - NUmber of samples in second coordinate.
* @param b1 - First betti number.
* @param b2 - Second betti number.
* 
*/
extern std::pair<std::vector<Vertex>,std::vector<uint>> meshFromSurface(
    vec3 (*param)(float, float), const int uCount, const int vCount, const int b1, const int b2);


class Mesh : public PrimitiveData<Vertex>
{
public:
    Mesh();
};

class MeshGen 
{
public:
    MeshGen();
    ~MeshGen();

    void setShaderManager(ShaderManager* shaderManager);
    void polylineMesh(Buffer& lines, Buffer& instances, PrimitiveData<Vertex>& mesh, uint resolution = 10);
private:
    ShaderManager * shaders;
    Buffer tempData;
};

#endif
