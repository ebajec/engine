#include "box_debug_view.h"

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

	Mesh3DCreateInfo ci = {
		.data = verts.data(),
		.vcount = verts.size(),
		.indices = indices.data(),
		.icount = indices.size()
	};

	table->upload(model, "model3d", &ci);

	boxes.clear();
	oboxes.clear();
}
