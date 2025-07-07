#include "resource_loader.h"

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

extern ResourceFns g_shader_loader_fns;

extern Handle load_shader_file(ResourceLoader *loader, std::string_view path);
extern const GLShaderModule *get_shader(ResourceLoader *loader, Handle h);

