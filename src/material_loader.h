#include "def_gl.h"
#include "resource_table.h"

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

struct MaterialCreateInfo
{
	std::string path;
};

extern ResourceAllocFns g_material_alloc_fns;

ResourceHandle load_material_file(ResourceTable *loader, std::string_view path);

const GLMaterial *get_material(ResourceTable *loader, ResourceHandle h);
