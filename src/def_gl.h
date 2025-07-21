#ifndef DEF_GL_H
#define DEF_GL_H

#include <stdint.h>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

typedef uint32_t gl_tex;
typedef uint32_t gl_renderbuffer;
typedef uint32_t gl_framebuffer;
typedef uint32_t gl_ubo;
typedef uint32_t gl_vao;
typedef uint32_t gl_vbo;
typedef uint32_t gl_program;

typedef uint32_t ResourceHandle;
typedef ResourceHandle ModelID;
typedef ResourceHandle ShaderID;
typedef ResourceHandle TextureID;
typedef ResourceHandle MaterialID;

enum TexFormat
{
	TEX_FORMAT_RGBA8
};

enum ModelType
{
	MODEL_TYPE_MESH_3D,
	MODEL_TYPE_MESH_2D
};

enum gl_renderer_bindings
{
	GL_RENDERER_COLOR_ATTACHMENT_BINDING = 0,
	GL_RENDERER_FRAMEDATA_BINDING = 5
};

struct gl_framedata_t
{
	glm::mat4 p;
	glm::mat4 v;
	glm::mat4 pv;
	glm::vec3 center;
	float t;
};

struct gl_tex_quad
{
	gl_vao vao;
	gl_vbo vbo;
	gl_vbo ibo;
};

struct gl_render_target
{
	uint32_t w;
	uint32_t h;

	gl_framebuffer fbo;
	gl_tex color;
	gl_renderbuffer depth;
	gl_ubo ubo;
	
	// these are for drawing to a window
	~gl_render_target();
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
