#ifndef RENDERER_TYPE_H
#define RENDERER_TYPE_H

#include <stdint.h>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

typedef uint32_t ResourceHandle;

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

enum ImgFormat
{
	IMG_FORMAT_RGBA8
};

enum ModelType
{
	MODEL_TYPE_MESH_3D,
	MODEL_TYPE_MESH_2D
};

struct Framedata
{
	glm::mat4 p;
	glm::mat4 v;
	glm::mat4 pv;
	glm::vec3 center;
	float t;
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
