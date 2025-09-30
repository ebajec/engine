#ifndef BOX_DISPLAY_H
#define BOX_DISPLAY_H

#include "utils/geometry.h"

#include <resource/resource_table.h>
#include <resource/model_loader.h>
#include <resource/material_loader.h>

#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>

#include <vector>

struct BoxDebugView
{
	std::vector<aabb3_t> boxes;
	ModelID model;
	MaterialID material;

	ResourceTable *table;

	BoxDebugView(ResourceTable *table) : table(table) {
		model = model_create(table);
		material = material_load_file(table, "material/box_debug.yaml");
	
	}

	void clear() {boxes.clear();}
	void add(aabb3_t box){boxes.push_back(box);}
	void update();
};

#endif
