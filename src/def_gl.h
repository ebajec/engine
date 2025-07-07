#ifndef DEF_GL_H
#define DEF_GL_H

#include <stdint.h>
#include <glm/vec4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

#include <memory>
#include <string>

typedef uint32_t gl_tex;
typedef uint32_t gl_renderbuffer;
typedef uint32_t gl_framebuffer;
typedef uint32_t gl_ubo;
typedef uint32_t gl_vao;
typedef uint32_t gl_vbo;
typedef uint32_t gl_program;

typedef uint32_t Handle;
typedef Handle ModelID;
typedef Handle ShaderID;
typedef Handle TextureID;
typedef Handle MaterialID;

enum TexFormat
{
	TEX_FORMAT_RGBA8
};

enum ModelType
{
	MODEL_TYPE_MESH_3D,
	MODEL_TYPE_MESH_2D
};

struct vertex2d
{
	glm::vec2 position;
	glm::vec2 uv;
};

struct vertex3d
{
	glm::vec3 postion;
	glm::vec2 uv;
	glm::vec3 normal;
};

#endif
