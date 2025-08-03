#include "renderer_defaults.h"
#include "material_loader.h"
#include "texture_loader.h"
#include "model_loader.h"

#include <vector>

LoadResult renderer_defaults_init(ResourceTable *table, RendererDefaults *defaults) 
{
	LoadResult result = RESULT_SUCCESS;
	defaults->materials.screen_quad = load_material_file(table,"material/screen_quad.yaml");

	if (!defaults->materials.screen_quad) 
		return RESULT_ERROR;

	//-----------------------------------------------------------------------------
	// screen quad
	
	Mesh2DCreateInfo mesh_info = {
		.data = g_default_tex_quad_verts,
		.vcount = sizeof(g_default_tex_quad_verts)/sizeof(vertex2d),
		.indices = g_default_tex_quad_indices,
		.icount = sizeof(g_default_tex_quad_indices)/sizeof(uint32_t),
	};
	
	defaults->models.screen_quad = ModelLoader::load_2d(table, &mesh_info);

	if (!defaults->models.screen_quad) 
		return RESULT_ERROR;

	//-----------------------------------------------------------------------------
	// missing texture
	
	uint32_t w = 16, h = 16;

	defaults->textures.missing = create_image_2d(table, w, h, TEX_FORMAT_RGBA8);

	uint32_t c[2] = {0xFF000000, 0xFFFF00FF};

	std::vector<uint32_t> data (w*h);

	uint32_t k = 0;
	for (uint32_t i = 0; i < w*h; ++i) {
		data[i] = c[k%2];
		k++;
		if (i%w == 0) k++;
	}

	table->upload(defaults->textures.missing,ImageLoader::name,data.data());

	if (!defaults->textures.missing) 
		return RESULT_ERROR;

	return result;
}
