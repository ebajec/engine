#ifndef RENDERER_DEFAULTS_H
#define RENDERER_DEFAULTS_H

#include "def_gl.h"
#include <resource/resource_table.h>

struct RendererDefaults 
{
	struct {
		MaterialID screen_quad;
	} materials;

	struct {
		TextureID missing;
	} textures;

	struct {	
		ModelID screen_quad;
	} models;
};

extern LoadResult renderer_defaults_init(ResourceTable *loader, RendererDefaults *defaults);

static constexpr vertex2d g_default_tex_quad_verts[] = {
	vertex2d{glm::vec2(-1,-1), glm::vec2(0, 0)},
	vertex2d{glm::vec2(1, -1), glm::vec2(1, 0)},
	vertex2d{glm::vec2(1,  1), glm::vec2(1, 1)},
	vertex2d{glm::vec2(-1, 1), glm::vec2(0, 1)}
};
static constexpr uint32_t g_default_tex_quad_indices[] = {
	0,1,2,0,2,3
};

#endif
