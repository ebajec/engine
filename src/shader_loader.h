#include "resource_table.h"

#include <memory>
#include <unordered_map>

struct GLShaderBindings
{
	std::unordered_map<std::string, uint32_t> ids;
};

struct GLShaderModule
{
	uint32_t stage;
	uint32_t id;

	std::unique_ptr<GLShaderBindings> bindings;
};

struct ShaderCreateInfo
{
	std::string path;

	bool operator == (const ShaderCreateInfo& other) const {
		return other.path == path;
	}
};

extern ResourceAllocFns g_shader_alloc_fns;

extern ResourceHandle load_shader_file(ResourceTable *loader, std::string_view path);
extern const GLShaderModule *get_shader(ResourceTable *loader, ResourceHandle h);

