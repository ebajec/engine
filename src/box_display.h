#ifndef BOX_DISPLAY_H
#define BOX_DISPLAY_H

#include "resource_loader.h"
#include "model_loader.h"
#include "material_loader.h"
#include "geometry.h"

#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <vector>

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

static inline void aabb3_edges( uint32_t offset, edge_t e[])
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

struct BoxDisplay
{
	std::vector<aabb3_t> boxes;
	ModelID model;
	MaterialID material;

	ResourceLoader *loader;

	BoxDisplay(ResourceLoader *loader) : loader(loader) {
		model = model_create(loader);
		material = load_material_file(loader, "material/box_debug.yaml");
	
	}

	void clear() {boxes.clear();}
	void add(aabb3_t box){boxes.push_back(box);}

	void update() 
	{
		std::vector<vertex3d> verts; 
		std::vector<uint32_t> indices;

		verts.reserve(boxes.size()*8);
		indices.resize(boxes.size()*12*2);

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

			aabb3_edges((uint32_t)idxv,(edge_t*)(indices.data() + idxi));
			idxi += 24;
			idxv = verts.size();
		}

		Mesh3DCreateInfo ci = {
			.data = verts.data(),
			.vcount = verts.size(),
			.indices = indices.data(),
			.icount = indices.size()
		};

		loader->upload(model, "model3d", &ci);

		boxes.clear();
	}
};

#endif
