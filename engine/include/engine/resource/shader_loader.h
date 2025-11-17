#ifndef SHADER_LOADER_H
#define SHADER_LOADER_H

#include "engine/resource/resource_table.h"

#include <memory>
#include <unordered_map>

extern ResourceAllocFns gl_shader_alloc_fns;

enum ShaderBindingType
{
	BINDING_TYPE_BUFFER,
	BINDING_TYPE_UNIFORM,
	BINDING_TYPE_IMAGE,
};

struct ShaderBinding
{
	uint32_t set;
	uint32_t id;
	ShaderBindingType type;
};

struct ShaderLayout
{
	std::unordered_map<std::string, uint32_t> slots;
	std::vector<ShaderBinding> bindings;
};

struct GLShaderModule
{
	uint32_t stage;
	uint32_t id;

	std::unique_ptr<ShaderLayout> bindings;
};

extern const GLShaderModule *get_shader(ResourceTable *loader, ResourceHandle h);

typedef ResourceHandle ShaderID;

struct ShaderCreateInfo
{
	std::string path;

	bool operator == (const ShaderCreateInfo& other) const {
		return other.path == path;
	}
};

extern ResourceHandle shader_load_file(ResourceTable *loader, std::string_view path);

#endif // SHADER_LOADER_H
