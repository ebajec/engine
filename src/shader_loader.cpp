#include "resource_loader.h"
#include "shader_loader.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include "spirv_reflect.h"
#pragma clang diagnostic push

#include <utils/log.h>

#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

typedef GLenum shader_stage_t;

static LoadResult gl_shader_module_create(ResourceLoader *loader, void **res, void *info);
static void gl_shader_module_destroy(ResourceLoader *loader, void *res);
static LoadResult gl_shader_load_file(ResourceLoader *loader, ResourceHandle h, const char *path);

ResourceFns g_shader_alloc_fns = {
	.create = &gl_shader_module_create,
	.destroy = &gl_shader_module_destroy,
	.load_file = &gl_shader_load_file 
};

static uint32_t compile_shader_spv(shader_stage_t stage, const uint32_t* data, size_t size) 
{
	GLuint s = glCreateShader(stage);

	glShaderBinary(1, &s,
               GL_SHADER_BINARY_FORMAT_SPIR_V_ARB,
               data, (GLsizei)(size*sizeof(uint32_t)));
	glSpecializeShaderARB(s,
                      "main",    
                      0, nullptr, nullptr);

	return s;
}

static shader_stage_t shader_stage_from_ext(std::string_view ext)
{
	shader_stage_t stage = 0;

	if (ext == ".comp")
		stage = GL_COMPUTE_SHADER;
	else if (ext == ".vert")
		stage = GL_VERTEX_SHADER;
	else if (ext == ".frag")
		stage = GL_FRAGMENT_SHADER;
	else if (ext == ".geom")
		stage = GL_GEOMETRY_SHADER;

	return stage;
}

static int spv_load(const fs::path& path,std::vector<uint32_t> &data)
{
	size_t size = fs::file_size(path);

	if (size % sizeof(uint32_t) != 0) {
		log_error("spv_load : file size is not aligned to 4 bytes");
		return -1;
	}

	data.resize(size/sizeof(uint32_t));

	std::ifstream file = std::ifstream(path, std::ios::binary);

	if (!file) {
		log_error("spv_load : failed to read spir-v");
		return -1;
	}

	file.read(reinterpret_cast<char*>(data.data()), (intptr_t)size);

	return 0;
}

static LoadResult spv_bindings_create(GLShaderBindings *shader_bindings, const void* code, size_t size)
{
	SpvReflectShaderModule module;

	SpvReflectResult result = spvReflectCreateShaderModule(size, code, &module);
	
	if (result != SPV_REFLECT_RESULT_SUCCESS)
		return RESULT_ERROR;

	std::vector<SpvReflectDescriptorBinding*> bindings;
	uint32_t binding_count = 0;

	result = spvReflectEnumerateDescriptorBindings(&module,&binding_count,nullptr); 
	bindings.resize(binding_count);
	result = spvReflectEnumerateDescriptorBindings(&module,&binding_count,bindings.data()); 

	for (uint32_t i = 0; i < binding_count; ++i) {
		SpvReflectDescriptorBinding *binding = bindings[i];
		const char *name = binding->name;
		uint32_t id = binding->binding;

		shader_bindings->ids[name] = id;
	}

	spvReflectDestroyShaderModule(&module);

	return result == SPV_REFLECT_RESULT_SUCCESS ? RESULT_SUCCESS : RESULT_ERROR;
}

static LoadResult check_shader(uint32_t id)
{
	GLint success = 0;
	glGetShaderiv(id, GL_COMPILE_STATUS, &success);

	if (success == GL_FALSE) {
		GLint length = 0;
		glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);

		std::vector<GLchar> error_message((size_t)length);
		glGetShaderInfoLog(id, length, NULL, &error_message[0]);

		if (length)
			printf("%s", &error_message[0]);

		glDeleteShader(id);
		return RESULT_SUCCESS;
	}

	return success ? RESULT_SUCCESS : RESULT_ERROR;
}

static LoadResult load_shader_file(fs::path file, GLShaderModule* out)
{
	std::string filestr = file.string(); 

	if (!fs::is_regular_file(file)) {
		return RESULT_ERROR;
	}

	std::string name = file.filename().string();
	std::string ext = file.extension().string();

	if (ext == ".spv") {
		const char* str = filestr.c_str();

		const char *c = str;
		while (*c) 
			c++;
		c--;

		// go to next .
		for(; c && *c != '.'; --c) {
		}

		const char* end = c;

		for(; c && *(--c) != '.'; --c) {
		}

		const char* start = c;

		if (c == str) {
			log_error("Wrong extension");
			return RESULT_ERROR;
		}

		size_t len = (size_t)(end - start);

		ext.resize(len);
		memcpy(ext.data(), start, len);

		name.resize(name.size() - sizeof(".spv") + 1);
	} else {
		log_error("Filename extension must be '.spv'");
		return RESULT_ERROR;
	}
 
	shader_stage_t stage = shader_stage_from_ext(ext);

	// not a shader, ignore
	if (!stage) {
		return RESULT_ERROR;
	}

	uint32_t id = 0;

	std::vector<uint32_t> code;
	if (spv_load(file,code) < 0) 
		return RESULT_ERROR;

	id = compile_shader_spv(stage, code.data(), code.size());

	if (!id) {
		log_error("While loading shader '%s' , failed to compile '%s'", name.c_str(), filestr.c_str());
		return RESULT_ERROR;
	}

	LoadResult res = check_shader(id);

	if (res) 
		return res;

	std::unique_ptr<GLShaderBindings> bindings (new GLShaderBindings);

	res = spv_bindings_create(bindings.get(), code.data(), code.size()*sizeof(uint32_t));

	if (res < 0) {
		return res;
	}

	log_info("loaded shader : %s",name.c_str());

	out->id = id;
	out->stage = stage;
	out->bindings = std::move(bindings);

	return res;
}

LoadResult gl_shader_module_create(ResourceLoader *loader, void **res, void *info)
{
	ShaderCreateInfo *desc = static_cast<ShaderCreateInfo*>(info);
	std::unique_ptr<GLShaderModule> shader (new GLShaderModule{});

	LoadResult result = (LoadResult)load_shader_file(desc->path, shader.get());

	if (result != RESULT_SUCCESS)
		return result;

	*res = shader.release();
	return result;

}

void gl_shader_module_destroy(ResourceLoader *loader, void *res)
{
	if (!res) 
		return;

	GLShaderModule *shader = static_cast<GLShaderModule*>(res);
	if (shader->id) glDeleteShader(shader->id);
}

static LoadResult gl_shader_load_file(ResourceLoader *loader, ResourceHandle h, const char *path)
{
	ShaderCreateInfo ci = {
		.path = path
	};
	ci.path.append(".spv");

	LoadResult result = loader->allocate(h,&ci);

	if (result != RESULT_SUCCESS) {
		return result;
	}

	return result;
}

ResourceHandle load_shader_file(ResourceLoader *loader, std::string_view path)
{
	if (ResourceHandle h = loader->find(path)) 
		return h;

	ResourceHandle h = loader->create_handle(RESOURCE_TYPE_SHADER);

	LoadResult result = loader->load_file(h,path.data());

	if (result != RESULT_SUCCESS)
		goto error_cleanup;

	loader->set_handle_key(h,path);
	return h;

error_cleanup:
	log_error("Failed to load shader file at %s",path.data());
	loader->destroy_handle(h);
	return RESOURCE_HANDLE_NULL;
}

const GLShaderModule *get_shader(ResourceLoader *loader, ResourceHandle h)
{
	const ResourceEntry *ent = loader->get(h);
	if (!ent || ent->type != RESOURCE_TYPE_SHADER)
		return nullptr;

	return static_cast<const GLShaderModule*>(ent->data);
}
