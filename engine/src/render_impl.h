#ifndef EV2_RENDER_IMPL_H
#define EV2_RENDER_IMPL_H

#include <engine/renderer/opengl.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#define VIEWDATA_BINDING 15
#define FRAMEDATA_BINDING 16

namespace ev2 {

struct FrameData
{
	float t;
	float dt;
};

struct ViewData
{
	glm::mat4 p;
	glm::mat4 v;
	glm::mat4 pv;
	glm::vec3 center;
};

struct RenderTarget
{
	uint32_t w;
	uint32_t h;

	GLuint fbo;
	GLuint color;
	GLuint depth;
};

};

#endif
