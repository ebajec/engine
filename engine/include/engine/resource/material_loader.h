#ifndef MATERIAL_LOADER_H
#define MATERIAL_LOADER_H

#include "engine/resource/texture_loader.h"
#include "engine/resource/shader_loader.h"
#include "engine/resource/resource_table.h"


struct GLTextureBinding
{
	TextureID id;
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

extern ResourceAllocFns g_material_alloc_fns;
const GLMaterial *get_material(ResourceTable *loader, ResourceHandle h);

typedef ResourceHandle MaterialID;

struct MaterialCreateInfo
{
	std::string path;
};
ResourceHandle load_material_file(ResourceTable *loader, std::string_view path);

#endif // MATERIAL_LOADER_H
