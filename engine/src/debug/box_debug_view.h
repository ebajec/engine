#ifndef BOX_DISPLAY_H
#define BOX_DISPLAY_H

#include "backends/vulkan/def_vulkan.h"

#include <ev2/utils/geometry.h>
#include <ev2/context.h>
#include <ev2/resource.h>
#include <ev2/render.h>

#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>

#include <vector>

struct BoxDebugView
{
	std::vector<aabb3_t> boxes;
	std::vector<obb_t> oboxes;

	size_t vcap;
	size_t vsize;
	ev2::BufferID vbo = EV2_NULL_HANDLE(Buffer);
	size_t icap;
	size_t isize;
	ev2::BufferID ibo = EV2_NULL_HANDLE(Buffer);

	ev2::GfxPipelineID pipeline;
	ev2::GfxContext *ctx;

	GLuint vao;

	uint64_t upload_index;

	BoxDebugView(ev2::GfxContext *_ctx) : ctx(_ctx) {
		pipeline = ev2::load_graphics_pipeline(ctx, "pipelines/box_debug.yaml");

		vcap = 0;
		icap = 0;

		glGenVertexArrays(1, &vao);
	
		glBindVertexArray(vao);

		glEnableVertexArrayAttrib(vao,0);
		glEnableVertexArrayAttrib(vao,1);
		glEnableVertexArrayAttrib(vao,2);

		glVertexAttribFormat(0,3,GL_FLOAT,0,offsetof(vertex3d,position));
		glVertexAttribFormat(1,2,GL_FLOAT,0,offsetof(vertex3d,uv));
		glVertexAttribFormat(2,3,GL_FLOAT,0,offsetof(vertex3d,normal));

		glVertexAttribBinding(0,0);
		glVertexAttribBinding(1,0);
		glVertexAttribBinding(2,0);

		glBindVertexArray(0);

	}

	void add(const aabb3_t &box){boxes.push_back(box);}
	void add(const obb_t &box){oboxes.push_back(box);}
	void update();
	void draw(ev2::PassCtx ctx);
};

#endif
