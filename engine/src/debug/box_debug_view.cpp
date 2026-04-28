#include "box_debug_view.h"

#include "backends/vulkan/context_impl.h"

#include <cstring>

struct edge_t 
{
	uint32_t u, v;
};

static inline void aabb3_corners(aabb3_t box, glm::dvec3 p[])
{
	p[0] = glm::dvec3(box.min.x, box.min.y, box.min.z);
	p[1] = glm::dvec3(box.max.x, box.min.y, box.min.z);
	p[2] = glm::dvec3(box.min.x, box.max.y, box.min.z);
	p[3] = glm::dvec3(box.max.x, box.max.y, box.min.z);

	p[4] = glm::dvec3(box.min.x, box.min.y, box.max.z);
	p[5] = glm::dvec3(box.max.x, box.min.y, box.max.z);
	p[6] = glm::dvec3(box.min.x, box.max.y, box.max.z);
	p[7] = glm::dvec3(box.max.x, box.max.y, box.max.z);
}

static inline void obb_corners(const obb_t &box, glm::dvec3 p[])
{
	p[0] = box.O + box.T*glm::dvec3(box.S.x,box.S.y,box.S.z);
	p[1] = box.O + box.T*glm::dvec3(box.S.x,box.S.y,-box.S.z);
	p[2] = box.O + box.T*glm::dvec3(box.S.x,-box.S.y,box.S.z);
	p[3] = box.O + box.T*glm::dvec3(box.S.x,-box.S.y,-box.S.z);
	p[4] = box.O + box.T*glm::dvec3(-box.S.x,box.S.y,box.S.z);
	p[5] = box.O + box.T*glm::dvec3(-box.S.x,box.S.y,-box.S.z);
	p[6] = box.O + box.T*glm::dvec3(-box.S.x,-box.S.y,box.S.z);
	p[7] = box.O + box.T*glm::dvec3(-box.S.x,-box.S.y,-box.S.z);
}

static inline void box_edges( uint32_t offset, edge_t e[])
{
	e[0] = {0,1};
	e[1] = {0,2};
	e[2] = {2,3};
	e[3] = {3,1};

	e[4] = {0,4};
	e[5] = {1,5};
	e[6] = {2,6};
	e[7] = {3,7};

	e[8]  = {4,5};
	e[9]  = {4,6};
	e[10] = {6,7};
	e[11] = {7,5};

	for (uint32_t i = 0; i < 12; ++i) {
		e[i].u += offset;
		e[i].v += offset;
	}
}


void BoxDebugView::update() 
{
	std::vector<vertex3d> verts; 
	std::vector<uint32_t> indices;

	size_t count = boxes.size() + oboxes.size();

	verts.reserve(count*8);
	indices.resize(count*12*2);

	size_t idxv = 0;
	size_t idxi = 0;
	for (aabb3_t box : boxes) {
		glm::dvec3 pts[8];

		aabb3_corners(box, pts);

		for (glm::dvec3 &p : pts) {
			verts.push_back(vertex3d{ 
				.position = p,
				.uv = glm::vec2(0),
				.normal = glm::vec3(0)
			});
		}

		box_edges((uint32_t)idxv,(edge_t*)(indices.data() + idxi));
		idxi += 24;
		idxv = verts.size();
	}

	for (obb_t box : oboxes) {
		glm::dvec3 pts[8];

		obb_corners(box, pts);

		for (glm::dvec3 &p : pts) {
			verts.push_back(vertex3d{ 
				.position = p,
				.uv = glm::vec2(0),
				.normal = glm::vec3(0)
			});
		}

		box_edges((uint32_t)idxv,(edge_t*)(indices.data() + idxi));
		idxi += 24;
		idxv = verts.size();
	}

	if (sizeof(vertex3d) * verts.size() > vcap) {
		vcap = verts.size() * sizeof(vertex3d);
		if (vbo.id)
			ev2::destroy_buffer(ctx, vbo);
		vbo = ev2::create_buffer(ctx, vcap);
	}
	vsize = verts.size() * sizeof(vertex3d);

	if (indices.size() * sizeof(uint32_t) > icap) {
		icap = indices.size() * sizeof(uint32_t);
		if (ibo.id)
			ev2::destroy_buffer(ctx, ibo);
		ibo = ev2::create_buffer(ctx, icap);
	}
	isize = indices.size() * sizeof(uint32_t);

	ev2::UploadContext uc = ev2::begin_upload(ctx, vsize, alignof(vertex3d)); 
	memcpy(uc.ptr, verts.data(), vsize);
	ev2::BufferUpload up = {
		.src_offset = 0,
		.dst_offset = 0,
		.size = vsize,
	};
	ev2::commit_buffer_uploads(ctx, uc, vbo, &up, 1);

	uc = ev2::begin_upload(ctx, isize, alignof(uint32_t));
	memcpy(uc.ptr, indices.data(), isize);
	up = {
		.src_offset = 0,
		.dst_offset = 0,
		.size = isize,
	};
	upload_index = ev2::commit_buffer_uploads(ctx, uc, ibo, &up, 1);

	ev2::flush_uploads(ctx);

	boxes.clear();
	oboxes.clear();
}

void BoxDebugView::draw(ev2::PassCtx pass)
{
	ev2::wait_complete(ctx, upload_index);
	if (!vbo.id || !ibo.id)
		return;

	ev2::cmd_bind_gfx_pipeline(pass.rec, pipeline);

	ev2::Buffer *vbo_obj = ctx->get_buffer(vbo);
	ev2::Buffer *ibo_obj = ctx->get_buffer(ibo);

	glBindVertexArray(vao);
	glBindVertexBuffer(0, vbo_obj->id, 0, sizeof(vertex3d));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_obj->id);

	uint32_t count = (uint32_t)(isize/(sizeof(uint32_t)));

	glDrawElements(GL_LINES, count, GL_UNSIGNED_INT, nullptr); 

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}
