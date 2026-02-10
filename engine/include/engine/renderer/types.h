#ifndef RENDERER_TYPE_H
#define RENDERER_TYPE_H

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

struct DrawCommand 
{
    unsigned int  count;
    unsigned int  instanceCount;
    unsigned int  firstIndex;
    int  baseVertex;
    unsigned int  baseInstance;
};

struct Camera
{
	glm::mat4 proj;
	glm::mat4 view;
};

struct vertex2d
{
	glm::vec2 position;
	glm::vec2 uv;
};

struct vertex3d
{
	glm::vec3 position;
	glm::vec2 uv;
	glm::vec3 normal;
};

#endif
