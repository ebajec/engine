#ifndef EV2_RENDER_IMPL_H
#define EV2_RENDER_IMPL_H

#include "backends/vulkan/def_vulkan.h"

#include <ev2/resource.h>
#include <ev2/render.h>

#include <glm/gtc/type_ptr.hpp>
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
	ImageID color;
	GLuint depth;

	RenderTargetAttachments attachments;
};

static inline ViewData view_data_from_matrices(float view[], float proj[])
{
	glm::mat4 view_mat = view ? glm::make_mat4(view) : glm::mat4(1.f);
	glm::mat4 proj_mat = proj ? glm::make_mat4(proj) : glm::mat4(1.f);

	return ViewData{
		.p = proj_mat,
		.v = view_mat,
		.pv = proj_mat * view_mat,
		.center = glm::vec3(0),
	};

}


};

#endif
