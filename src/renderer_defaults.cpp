#include "renderer_defaults.h"
#include "material_loader.h"
#include "texture_loader.h"
#include "model_loader.h"

LoadResult renderer_defaults_init(ResourceLoader *loader, RendererDefaults *defaults) 
{
	LoadResult result = RESULT_SUCCESS;
	defaults->materials.screen_quad = load_material_file(loader,"material/screen_quad.yaml");

	if (!defaults->materials.screen_quad) 
		return RESULT_ERROR;

	Mesh2DCreateInfo mesh_info = {
		.data = g_default_tex_quad_verts,
		.vcount = sizeof(g_default_tex_quad_verts)/sizeof(vertex2d),
		.indices = g_default_tex_quad_indices,
		.icount = sizeof(g_default_tex_quad_indices)/sizeof(uint32_t),
	};
	
	ResourceHandle h = loader->create_handle(RESOURCE_TYPE_MODEL);
	result = load_model(loader, h, &mesh_info);

	if (result != RESULT_SUCCESS) {
		loader->destroy_handle(h);
		return result;
	}
	defaults->models.screen_quad = h;
	defaults->textures.missing = load_image_file(loader, "image/eponge.png");

	if (!defaults->textures.missing) 
		return RESULT_ERROR;

	return result;
}
