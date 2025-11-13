#ifndef MATERIAL_LOADER_H
#define MATERIAL_LOADER_H

#include "engine/resource/texture_loader.h"
#include "engine/resource/shader_loader.h"
#include "engine/resource/resource_table.h"

extern ResourceAllocFns gl_material_alloc_fns;

struct GLTextureBinding
{
	ImageID id;
};

struct GLMaterial
{
	ShaderID vert;
	ShaderID frag;

	uint32_t program;

	// After loading the shaders, the reflection can be inspected 
	// to find the binding ids based on resource names in the material 
	// config
	std::unordered_map<uint32_t, GLTextureBinding> tex_bindings;
};

const GLMaterial *get_material(ResourceTable *loader, ResourceHandle h);

typedef ResourceHandle MaterialID;

struct MaterialCreateInfo
{
	std::string path;
};

extern ResourceHandle material_load_file(ResourceTable *rt, std::string_view path);
extern void material_bind_texture(ResourceTable *rt, MaterialID mat, const char * name, ImageID img);

#endif // MATERIAL_LOADER_H
