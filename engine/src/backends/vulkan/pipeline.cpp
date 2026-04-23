#include <ev2/pipeline.h>
#include <ev2/utils/ansi_colors.h>
#include <ev2/utils/log.h>

#include "vulkan/vulkan_core.h"

#include "backends/opengl/device_impl.h"
#include "backends/opengl/pipeline_impl.h"
#include "backends/opengl/resource_impl.h"
#include "backends/opengl/def_opengl.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include "spirv_reflect.h"
#pragma clang diagnostic push

#include <yaml-cpp/yaml.h>

#include <string>
#include <vector>
#include <vector>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

//------------------------------------------------------------------------------
// OpenGL shader loading

typedef GLenum shader_stage_t;

static GLuint compile_spv_gl(shader_stage_t stage, const uint32_t* data, size_t size) 
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

	return stage;
}

static int read_spv_file(const fs::path& path, std::vector<uint32_t> &data)
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

ev2::DescriptorType spv_reflect_binding_type_to_internal(SpvReflectDescriptorType type)
{
	switch (type){
		case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER: 
			return ev2::DESCRIPTOR_TYPE_SAMPLER;
		case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: 
			return ev2::DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE: 
			return ev2::DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE: 
			return ev2::DESCRIPTOR_TYPE_STORAGE_IMAGE;
		case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: 
			return ev2::DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
		case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: 
			return ev2::DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
		case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER: 
			return ev2::DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER: 
			return ev2::DESCRIPTOR_TYPE_STORAGE_BUFFER;
		case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: 
			return ev2::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: 
			return ev2::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
		case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: 
			return ev2::DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		default:
			return ev2::DESCRIPTOR_TYPE_MAX_ENUM;
	}
}

static ev2::Result get_spv_module_layout(ev2::DescriptorLayout *layout, 
									  const void* code, size_t size)
{
	SpvReflectShaderModule module;

	SpvReflectResult result = spvReflectCreateShaderModule(size, code, &module);
	
	if (result != SPV_REFLECT_RESULT_SUCCESS)
		return ev2::ELOAD_FAILED;

	std::vector<SpvReflectDescriptorBinding*> bindings;
	uint32_t binding_count = 0;

	result = spvReflectEnumerateDescriptorBindings(
		&module,&binding_count,nullptr); 
	bindings.resize(binding_count);
	result = spvReflectEnumerateDescriptorBindings(
		&module,&binding_count,bindings.data()); 

	for (uint32_t i = 0; i < binding_count; ++i) {
		SpvReflectDescriptorBinding *binding = bindings[i];
		std::string tmpname;

		const char *name = binding->name;

		bool no_instance = !name || *name == '\0'; 

		if (no_instance && 
			binding->type_description &&
			binding->type_description->type_name
		) {
			name = binding->type_description->type_name;
		}
		else if (no_instance) {
			tmpname = 
				"set" + std::to_string(binding->set) + 
				"binding" + std::to_string(binding->binding);
		}

		uint32_t id = binding->binding;

		layout->bindings[name] = {
			.type = spv_reflect_binding_type_to_internal(binding->descriptor_type),
			.set = binding->set,
			.id = id,
		};
	}

	spvReflectDestroyShaderModule(&module);

	return result == SPV_REFLECT_RESULT_SUCCESS ? ev2::SUCCESS : ev2::ELOAD_FAILED;
}

static ev2::Result check_shader_gl(uint32_t id)
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
		return ev2::ELOAD_FAILED;
	}

	return success ? ev2::SUCCESS : ev2::ELOAD_FAILED;
}

ev2::ShaderStage gl_stage_to_ev2(GLenum stage)
{
	switch (stage) {
		case GL_COMPUTE_SHADER: return ev2::STAGE_COMPUTE;
		case GL_VERTEX_SHADER: return ev2::STAGE_VERTEX;
		case GL_FRAGMENT_SHADER: return ev2::STAGE_FRAGMENT;
		default: return ev2::STAGE_MAX_ENUM;
	};
}

std::string get_layout_string(const ev2::DescriptorLayout& layout)
{
	std::string info;
	for (const auto [name, binding] : layout.bindings) {
		info += "\t(set " + std::to_string(binding.set) 
			+ ", index " + std::to_string(binding.id) + ") : " 
			 + name + "\n";	
	}
	if (!info.empty())
		info.erase(info.size() - 1);

	return info;
}

std::string get_shader_info(ev2::Shader *shader)
{
	const char *stage;
	switch (shader->stage) {
		case ev2::STAGE_VERTEX: stage = "vertex"; break;
		case ev2::STAGE_FRAGMENT: stage = "fragment"; break;
		case ev2::STAGE_COMPUTE: stage = "compute"; break;
		default:
			stage = "???";
	}

	std::string info;
	info += "\tstage : " + std::string(stage) + "\n";
	info += get_layout_string(*shader->layout);

	return info;

}

static ev2::Result load_shader_file(const char *path, ev2::Shader* out)
{
	fs::path file (path);

	std::string filestr = file.string(); 

	if (!fs::is_regular_file(file)) {
		log_error("File does not exist: %s", filestr.c_str());
		return ev2::ELOAD_FAILED;
	}

	std::string ext = file.extension().string();

	// turn .stage.spv into .stage
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
			return ev2::ELOAD_FAILED;
		}

		size_t len = (size_t)(end - start);

		ext.resize(len);
		memcpy(ext.data(), start, len);
	} else {
		log_error("Filename extension must be '.spv'");
		return ev2::ELOAD_FAILED;
	}
 
	shader_stage_t stage = shader_stage_from_ext(ext);

	// not a shader, ignore
	if (!stage) {
		return ev2::ELOAD_FAILED;
	}

	std::vector<uint32_t> code;
	if (read_spv_file(file,code) < 0) 
		return ev2::ELOAD_FAILED;

	auto layout = std::make_unique<ev2::DescriptorLayout>();

	ev2::Result res = get_spv_module_layout(
		layout.get(), code.data(), code.size()*sizeof(uint32_t));

	GLuint id = compile_spv_gl(stage, code.data(), code.size());

	if (!id) {
		log_error("While loading shader '%s' , failed to compile '%s'", 
			path, filestr.c_str());
		return ev2::ELOAD_FAILED;
	}

	res = check_shader_gl(id);

	if (res) 
		return res;

	if (res < 0) {
		return res;
	}

	out->id = id;
	out->stage = gl_stage_to_ev2(stage);
	out->layout = std::move(layout);

	return res;
}

ev2::Result gl_shader_create(ev2::Device *dev, ev2::Shader *p_shader, const char *path)
{
	std::string syspath = dev->assets->get_system_path(path);
	ev2::Result res = load_shader_file(syspath.c_str(), p_shader);

	if (res != ev2::SUCCESS)
		return res;

	std::string info_str = get_shader_info(p_shader);
	log_info("Shader : "COLORIZE_PATH(%s)"\n%s",path, info_str.c_str());

	return res;
}

void gl_shader_destroy(ev2::Device *dev, void *usr)
{
	if (!usr) 
		return;

	ev2::Shader *shader = static_cast<ev2::Shader*>(usr);

	if (shader->id) 
		glDeleteShader(shader->id);

	delete shader;
}

ev2::Result gl_shader_reload(ev2::Device *dev, void **usr, const char *path)
{
	ev2::Shader new_shader{};

	ev2::Result res = gl_shader_create(dev, &new_shader, path);

	if (res != ev2::SUCCESS)
		return res;
	
	ev2::Shader *shader = static_cast<ev2::Shader*>(*usr);

	if (shader->id)
		glDeleteShader(shader->id);

	*shader = std::move(new_shader);
	return res;
}

//------------------------------------------------------------------------------
// OpenGL graphics pipeline loading

struct GfxPipelineInfo
{
	std::string vert; 
	std::string frag; 
};

ev2::Result parse_gfx_pipeline_file(GfxPipelineInfo *info, const char *path)
{
	YAML::Node root = YAML::LoadFile(path);

	const YAML::Node &shaders_node = root["shaders"];

	if (!shaders_node) {
		log_error("Pipeline %s does not specify any shaders!",path);
		return ev2::ELOAD_FAILED;
	}

	if (!shaders_node.IsMap()) {
		log_error("'shaders' field is not a map!");
		return ev2::ELOAD_FAILED;
	}

	const YAML::Node &vert = shaders_node["vert"];
	const YAML::Node &frag = shaders_node["frag"];

	if (!frag) {
		log_error("Pipeline %s does not contain a fragment shader",path);
		return ev2::ELOAD_FAILED;
	}

	if (!vert) {
		log_error("Pipeline %s does not contain a vertex shader", path);
		return ev2::ELOAD_FAILED;
	}

	info->frag = frag.as<std::string>();
	info->vert = vert.as<std::string>();

	return ev2::SUCCESS;
}

size_t spv_input_var_size(SpvReflectInterfaceVariable *var)
{
	size_t size = var->numeric.scalar.width/8;

	if (var->members) {
		for (uint32_t i = 0; i < var->member_count; ++i) {
			size += spv_input_var_size(&var->members[i]);
		}
	} else if (var->numeric.vector.component_count) {
		size *= var->numeric.vector.component_count;
	} else if (var->numeric.matrix.column_count) {
		size *= var->numeric.matrix.column_count * var->numeric.matrix.row_count;
	}

	for (uint32_t i = 0; i < var->array.dims_count; ++i) {
		size *= var->array.dims[i];
	}

	return size;
}

struct VertexInputLayout
{
	std::vector<VkVertexInputAttributeDescription> desc;
	size_t stride;
};

ev2::Result parse_vertex_layout(const char *path, VertexInputLayout *p_out)
{
	std::vector<uint32_t> data;
	if (read_spv_file(path, data) < 0)
		return ev2::ELOAD_FAILED;

	SpvReflectShaderModule reflection;
	SpvReflectResult spv_res = spvReflectCreateShaderModule(
		data.size()*sizeof(uint32_t), 
		data.data(), 
		&reflection);

	if (spv_res != SPV_REFLECT_RESULT_SUCCESS)
		return ev2::ELOAD_FAILED;

	uint32_t in_count;
	spvReflectEnumerateInputVariables(&reflection, &in_count, nullptr);

	std::vector<SpvReflectInterfaceVariable*> vars (in_count);
	spvReflectEnumerateInputVariables(&reflection, &in_count, vars.data());

	std::sort(vars.begin(), vars.end(), [](
				const SpvReflectInterfaceVariable *a,
				const SpvReflectInterfaceVariable *b) {
				return a->location < b->location;
		   });

	size_t offset = 0;

	std::vector<VkVertexInputAttributeDescription> attributes;
	attributes.reserve(in_count);

	for (uint32_t i = 0; i < in_count; ++i) {
		SpvReflectInterfaceVariable* var = vars[i];
		if (var->built_in >= 0)
			continue;

		VkVertexInputAttributeDescription desc = {
			.binding = 0,
			.location = var->location,
			.format = (VkFormat)var->format,
			.offset = offset
		};

		size_t size = spv_input_var_size(var);
		offset += size;

		attributes.push_back(desc);
	}
	spvReflectDestroyShaderModule(&reflection);

	p_out->desc = std::move(attributes);
	p_out->stride = offset;

	return ev2::SUCCESS;
}

static GLuint gen_gl_vao_from_vtx_layout(const VertexInputLayout *p_layout)
{
	GLuint vao;
	glCreateVertexArrays(1, &vao);
	glBindVertexArray(vao);

	GLsizei stride = p_layout->stride;

	for (const VkVertexInputAttributeDescription &desc : p_layout->desc) {
		GLenum type = GL_FLOAT;
		GLint scalar_size;

		switch (desc.format) {
  			case VK_FORMAT_UNDEFINED:
				continue;

  			case VK_FORMAT_R32_UINT:
  			case VK_FORMAT_R32_SINT:
  			case VK_FORMAT_R32_SFLOAT:
				scalar_size = 1;
				break;

  			case VK_FORMAT_R32G32_UINT:
  			case VK_FORMAT_R32G32_SINT:
  			case VK_FORMAT_R32G32_SFLOAT:
				scalar_size = 2;
				break;

  			case VK_FORMAT_R32G32B32_UINT:
  			case VK_FORMAT_R32G32B32_SINT:
  			case VK_FORMAT_R32G32B32_SFLOAT:
				scalar_size = 3;
				break;

  			case VK_FORMAT_R32G32B32A32_UINT:
  			case VK_FORMAT_R32G32B32A32_SINT:
			case VK_FORMAT_R32G32B32A32_SFLOAT:
				scalar_size = 4;
				break;
		}

		switch (desc.format) {
  			case VK_FORMAT_UNDEFINED:
				continue;

  			case VK_FORMAT_R32_UINT:
  			case VK_FORMAT_R32G32_UINT:
  			case VK_FORMAT_R32G32B32_UINT:
  			case VK_FORMAT_R32G32B32A32_UINT:
				type = GL_UNSIGNED_INT;
				break;
  			case VK_FORMAT_R32_SINT:
  			case VK_FORMAT_R32G32_SINT:
  			case VK_FORMAT_R32G32B32_SINT:
  			case VK_FORMAT_R32G32B32A32_SINT:
				type = GL_INT;	
				break;
  			case VK_FORMAT_R32_SFLOAT:
  			case VK_FORMAT_R32G32_SFLOAT:
  			case VK_FORMAT_R32G32B32_SFLOAT:
			case VK_FORMAT_R32G32B32A32_SFLOAT:
				type = GL_FLOAT;
				break;
		}

		glEnableVertexArrayAttrib(vao,desc.location);
		glVertexAttribFormat(desc.location,scalar_size,type,0,desc.offset);
		glVertexAttribBinding(desc.location,0);
	}

	return vao;
}

static ev2::Result init_gfx_pipeline_shaders(
	ev2::Device *dev, 
	ev2::GraphicsPipeline *pipe, 
	const char *vert_path, 
	const char *frag_path
) 
{
	ev2::Result res = ev2::SUCCESS;

	ev2::ShaderID vert_handle = ev2::load_shader(dev, vert_path);
	if (!vert_handle.id) {
		res = ev2::EUNKNOWN;
		return res;
	}

	ev2::ShaderID frag_handle = ev2::load_shader(dev, frag_path);
	if (!frag_handle.id) {
		res = ev2::EUNKNOWN;
		return res;
	}

	const ev2::Shader *vert = dev->assets->get<ev2::Shader>(vert_handle.id);
	const ev2::Shader *frag = dev->assets->get<ev2::Shader>(frag_handle.id);

	VertexInputLayout vertex_layout {};

	std::string sys_vert_path = dev->assets->get_system_path(vert_path);
	parse_vertex_layout(sys_vert_path.c_str(), &vertex_layout);

	GLuint vao = gen_gl_vao_from_vtx_layout(&vertex_layout);

	uint32_t program = glCreateProgram();
	glAttachShader(program, vert->id);
	glAttachShader(program, frag->id);
	glLinkProgram(program);

	std::string name = std::string(vert_path) + "-" + std::string(frag_path);

	if (!gl_check_program(program,name.c_str())) {
		glDeleteProgram(program);
		return ev2::EUNKNOWN;
	}

	pipe->program = program;
	pipe->vao = vao;
	pipe->vert = vert_handle;
	pipe->frag = frag_handle;

	typedef std::unordered_map<
		std::string,
		ev2::DescriptorBinding
	> bind_map_t; 

	bind_map_t merged_ids;

	if (vert->layout) {
		merged_ids.merge(vert->layout->bindings);
	}
	if (frag->layout) {
		merged_ids.merge(frag->layout->bindings);
	}

	pipe->layout.bindings = std::move(merged_ids);

	return res;
}

std::string get_gfx_pipeline_info(ev2::Device *dev, ev2::GraphicsPipeline *p_pipeline)
{
	const char *vert = dev->assets->get_entry((AssetID)p_pipeline->vert.id)->path;
	const char *frag = dev->assets->get_entry((AssetID)p_pipeline->frag.id)->path;

	const char *fmt = 
		"\tvert: "COLORIZE_PATH(%s)"\n"
		"\tfrag: "COLORIZE_PATH(%s)"";

	std::string buf;
	buf.resize((strlen(fmt) + strlen(vert) + strlen(frag)) + 1);
	
	snprintf(buf.data(), buf.size(), fmt, vert, frag);
	return buf;
}

static ev2::Result gl_gfx_pipeline_create(
	ev2::Device *dev, 
	ev2::GraphicsPipeline *p_pipeline, 
	const char *path
)
{
	ev2::Result result = ev2::SUCCESS;
	std::string syspath = dev->assets->get_system_path(path);

	if (!fs::exists(syspath)) {
		log_error("File does not exist: %s", syspath.c_str());
		return ev2::ELOAD_FAILED;
	}

	GfxPipelineInfo gfx_info;
	try {
		result = parse_gfx_pipeline_file(&gfx_info,syspath.c_str());
	} catch (const YAML::Exception& e) {
		log_error("Error while parsing YAML: %s",e.what());
		return ev2::ELOAD_FAILED;
	} catch (const YAML::BadFile& e) {
		log_error("Error while parsing YAML: %s",e.what());
		return ev2::ELOAD_FAILED;
	} catch (...) {
		log_error("Error while parsing YAML: unknown");
		return ev2::ELOAD_FAILED;
	}

	if (result) 
		return result;

	result = init_gfx_pipeline_shaders(
		dev, 
		p_pipeline, 
		gfx_info.vert.c_str(), 
		gfx_info.frag.c_str()
	);

	if (result) {
		log_error("Failed to initialize shaders for %s", path);
		return result;
	}

	std::string info = get_gfx_pipeline_info(dev, p_pipeline);
	log_info("Graphics pipeline : "COLORIZE_PATH(%s)"\n%s", path, info.c_str());

	return result;
}

static void gl_gfx_pipeline_destroy(ev2::Device *dev, void* usr) 
{
	ev2::GraphicsPipeline *pipeline = static_cast<ev2::GraphicsPipeline*>(usr);

	if (pipeline->program)
		glDeleteProgram(pipeline->program);

	ev2::unload_shader(dev, pipeline->vert);
	ev2::unload_shader(dev, pipeline->frag);

	delete pipeline;
}

static ev2::Result gl_gfx_pipeline_reload(ev2::Device *dev, void** usr, const char *path)
{
	ev2::GraphicsPipeline new_pipeline{};
	ev2::Result res = gl_gfx_pipeline_create(dev, &new_pipeline, path);

	if (res != ev2::SUCCESS)
		return res;

	ev2::GraphicsPipeline *pipeline = static_cast<ev2::GraphicsPipeline*>(*usr);

	if (pipeline->program)
		glDeleteProgram(pipeline->program);

	*pipeline = std::move(new_pipeline);
	return res;
}

//------------------------------------------------------------------------------
// Compute shaders

static ev2::Result gl_compute_pipeline_create(
	ev2::Device *dev, 
	ev2::ComputePipeline *p_pipeline, 
	const char *path
)
{
	ev2::Shader *p_shader = &p_pipeline->shader;
	ev2::Result result = gl_shader_create(dev, p_shader, path);

	if (result)
		return result;

	if (p_shader->stage != ev2::STAGE_COMPUTE) {
		gl_shader_destroy(dev, p_shader);
		return ev2::ELOAD_FAILED;
	}

	GLuint program = glCreateProgram();
	glAttachShader(program, p_shader->id);
	glLinkProgram(program);

	if (!gl_check_program(program, path)) {
		glDeleteProgram(program);
		return ev2::ELOAD_FAILED;
	}

	p_pipeline->program = program;

	return result;
}

static void gl_compute_pipeline_destroy(ev2::Device *dev, void* usr) 
{
	ev2::ComputePipeline *pipeline = static_cast<ev2::ComputePipeline*>(usr);

	if (pipeline->program)
		glDeleteProgram(pipeline->program);

	glDeleteShader(pipeline->shader.id);

	delete pipeline;
}

static ev2::Result gl_compute_pipeline_reload(ev2::Device *dev, void** usr, const char *path)
{
	ev2::ComputePipeline new_pipeline{};

	ev2::Result res = gl_compute_pipeline_create(dev, &new_pipeline, path);

	if (res != ev2::SUCCESS)
		return res;
	
	ev2::ComputePipeline *pipeline = static_cast<ev2::ComputePipeline*>(*usr);
	*pipeline = std::move(new_pipeline);
	return res;
}

//------------------------------------------------------------------------------
// Interface

namespace ev2 {

//------------------------------------------------------------------------------
// Shaders

ShaderID load_shader(Device *dev, const char *path)
{
	AssetID id = dev->assets->load(path);
	if (id)
		return ShaderID{id};

	static AssetVTable vtbl = {
		.reload = gl_shader_reload,
		.destroy = gl_shader_destroy,
	};

	ev2::Shader *shader = new ev2::Shader{}; 

	ev2::Result res = gl_shader_create(dev, shader, path);

	if (res == ev2::SUCCESS) {
		id = dev->assets->allocate(&vtbl, shader, path); 
		return ShaderID{id};
	}

	delete shader;

	return EV2_NULL_HANDLE(Shader);
}

void unload_shader(Device *dev, ShaderID shader)
{
}

//------------------------------------------------------------------------------

GraphicsPipelineID load_graphics_pipeline(Device *dev, const char *path)
{
	AssetID id = dev->assets->load(path);

	if (id)
		return GraphicsPipelineID{id};

	static AssetVTable vtbl = {
		.reload = gl_gfx_pipeline_reload,
		.destroy = gl_gfx_pipeline_destroy,
	};

	ev2::GraphicsPipeline *pipeline = new ev2::GraphicsPipeline{}; 
	ev2::Result res = gl_gfx_pipeline_create(dev, pipeline, path);

	if (res == ev2::SUCCESS) {
		id = dev->assets->allocate(&vtbl, pipeline, path);

		if (dev->assets->reloader) {
			dev->assets->reloader->add_dependency(pipeline->vert.id, id);
			dev->assets->reloader->add_dependency(pipeline->frag.id, id);
		}

		return GraphicsPipelineID{id};
	}

	delete pipeline;
	return EV2_NULL_HANDLE(GraphicsPipeline);
}

void unload_graphics_pipeline(Device *dev, GraphicsPipelineID pipe)
{
}

//------------------------------------------------------------------------------

ComputePipelineID load_compute_pipeline(Device *dev, const char *path)
{
	AssetID id = dev->assets->load(path);

	if (id)
		return ComputePipelineID{id};

	static AssetVTable vtbl = {
		.reload = gl_compute_pipeline_reload,
		.destroy = gl_compute_pipeline_destroy,
	};

	ev2::ComputePipeline *pipeline = new ev2::ComputePipeline{}; 
	ev2::Result res = gl_compute_pipeline_create(dev, pipeline, path);

	if (res == ev2::SUCCESS) {
		id = dev->assets->allocate(&vtbl, pipeline, path);
		return ComputePipelineID{id};
	}

	delete pipeline;
	return EV2_NULL_HANDLE(ComputePipeline);
}

void unload_compute_pipeline(Device *dev, ComputePipelineID pipe)
{
}

//------------------------------------------------------------------------------

DescriptorLayoutID get_graphics_pipeline_layout(Device *dev, GraphicsPipelineID pipe)
{
	const ev2::GraphicsPipeline * res = dev->assets->get<ev2::GraphicsPipeline>(pipe.id);

	if (!res)
		return EV2_NULL_HANDLE(DescriptorLayout);

	const DescriptorLayout *p_layout = &res->layout;
	return DescriptorLayoutID{.id = reinterpret_cast<uint64_t>(&res->layout)};
}

DescriptorLayoutID get_compute_pipeline_layout(Device *dev, ComputePipelineID pipe)
{
	if (!pipe.id)
		return EV2_NULL_HANDLE(DescriptorLayout);

	const ev2::ComputePipeline * res = dev->assets->get<ev2::ComputePipeline>(pipe.id);
	const DescriptorLayout *p_layout = res->shader.layout.get();

	return DescriptorLayoutID{.id = reinterpret_cast<uint64_t>(p_layout)};
}

BindingSlot find_binding(DescriptorLayoutID id, const char *name)
{
	DescriptorLayout *layout = EV2_TYPE_PTR_CAST(DescriptorLayout, id);

	auto it = layout->bindings.find(name);

	if (it != layout->bindings.end()) {
		return {.set = it->second.set, .id = it->second.id};
	}

	log_warn("Failed to find binding : %s", name);
	return {UINT16_MAX,UINT16_MAX};
}

DescriptorSetID create_descriptor_set(
	Device *dev, 
	DescriptorLayoutID layout_id, 
	uint16_t index
)
{
	DescriptorLayout * layout = EV2_TYPE_PTR_CAST(DescriptorLayout, layout_id);

	if (!layout)
		return EV2_NULL_HANDLE(DescriptorSet);

	DescriptorSet * set = new DescriptorSet{};
	set->index = index;

	for (const auto& [name, bind] : layout->bindings) {
	  	ResourceBinding binding = {
	  		.type = bind.type
	  	};
		set->bindings[bind.id] = binding;	
	}

	if (set->bindings.empty()) {
		log_warn("No bindings initialized for pipeline");
	}

	return EV2_HANDLE_CAST(DescriptorSet, set);
}

void destroy_descriptor_set(Device *dev, DescriptorSetID id)
{
	DescriptorSet *set = EV2_TYPE_PTR_CAST(DescriptorSet, id);

	delete set;
}

ev2::Result bind_buffer(
	Device *dev, 
	DescriptorSetID set_id, 
	BindingSlot slot, 
	BufferID buf_id, 
	size_t offset, 
	size_t size
) 
{
	DescriptorSet *set = EV2_TYPE_PTR_CAST(DescriptorSet, set_id);
	//ev2::Buffer *buf = dev->rt->get<ev2::Buffer>(buf_id);

	if (set->index != slot.set) {
		log_error(
			"Mismatched binding for buffer %d. (set=%d, index=%d)",
			buf_id, slot.set, slot.id, set->index
		);
		return ev2::EINVALID_BINDING;
	}

	auto it = set->bindings.find(slot.id);

	if (it == set->bindings.end()) {
		log_error(
			"Mismatched binding for buffer %d. (set=%d, index=%d)",
			buf_id, slot.set, slot.id, set->index
		);
		return ev2::EINVALID_BINDING;
	}

	ResourceBinding *binding = &it->second;
	if (
		binding->type != DESCRIPTOR_TYPE_STORAGE_BUFFER && 
		binding->type != DESCRIPTOR_TYPE_UNIFORM_BUFFER &&
		binding->type != DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC &&
		binding->type != DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC
	) {
		log_error(
			"Mismatched binding type (%d) for buffer %d. (set=%d, index=%d)",
			binding->type, buf_id, slot.set, slot.id, set->index
		);
		return ev2::EINVALID_BINDING;
	}

	binding->buf = BufferBinding{
		.handle = buf_id,
		.size = size,
		.offset = offset,
	};
	return ev2::SUCCESS;
}

ev2::Result bind_texture(
	Device *dev, 
	DescriptorSetID set_id, 
	BindingSlot slot, 
	TextureID tex_id  
) 
{
	DescriptorSet *set = EV2_TYPE_PTR_CAST(DescriptorSet, set_id);
	Texture *tex = dev->get_texture(tex_id);

	if (set->index != slot.set) {
		log_error(
			"Mismatched binding for image %d. (set=%d, index=%d) to set %d",
			tex->img, slot.set, slot.id, set->index
		);
		return ev2::EINVALID_BINDING;
	}

	auto it = set->bindings.find(slot.id);

	if (it == set->bindings.end()) {
		log_error(
			"Attempting to bind image %d to nonexistent index %d in set %d", 
			tex->img, slot.id, slot.set);
		return ev2::EINVALID_BINDING;
	}

	ResourceBinding *binding = &it->second;

	if (
		binding->type != DESCRIPTOR_TYPE_SAMPLER && 
		binding->type != DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
		log_error(
			"Attempting to bind invalid resource (id=%d) to texture slot %d",
			binding->tex.handle, slot.id);
		return ev2::EINVALID_BINDING;
	}

	binding->tex = TextureBinding{.handle = tex_id};
	return ev2::SUCCESS;
}

ev2::Result bind_image(
	Device *dev,
	DescriptorSetID h_set, 
	BindingSlot slot, 
	ImageID h_img
)
{
	DescriptorSet *set = EV2_TYPE_PTR_CAST(DescriptorSet, h_set);
	Image *img = dev->get_image(h_img);

	if (set->index != slot.set) {
		log_error(
			"Mismatched binding for image %d. (set=%d, index=%d) to set %d",
			h_img.id, slot.set, slot.id, set->index
		);
		return ev2::EINVALID_BINDING;
	}

	auto it = set->bindings.find(slot.id);

	if (it == set->bindings.end()) {
		log_error(
			"Attempting to bind image %d to nonexistent index %d in set %d", 
			h_img.id, slot.id, slot.set);
		return ev2::EINVALID_BINDING;
	}

	ResourceBinding *binding = &it->second;

	if (binding->type != DESCRIPTOR_TYPE_STORAGE_IMAGE) {
		log_error(
			"Attempting to bind invalid resource (id=%d) to image slot %d",
			binding->tex.handle, slot.id);
		return ev2::EINVALID_BINDING;
	}

	binding->img = ImageBinding{.handle = h_img};

	return ev2::SUCCESS;
}

//------------------------------------------------------------------------------

RecorderID begin_commands(Device * dev, CommandMode mode)
{
	return EV2_HANDLE_CAST(Recorder, dev);
	return EV2_NULL_HANDLE(Recorder);
}

SyncID end_commands(RecorderID recorder)
{
	return EV2_NULL_HANDLE(Sync);
}

void submit(SyncID)
{
}

void cmd_bind_descriptor_set(RecorderID rec, DescriptorSetID set_id)
{
	Device *dev = EV2_TYPE_PTR_CAST(Device, rec);
	DescriptorSet *set = EV2_TYPE_PTR_CAST(DescriptorSet, set_id);

	for (auto [index, binding] : set->bindings) {
		switch(binding.type) {
			case ev2::DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			case ev2::DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			case ev2::DESCRIPTOR_TYPE_SAMPLER: {
				if (EV2_IS_NULL(binding.tex.handle))
					continue;

				Texture *tex = dev->get_texture(binding.tex.handle);
				Image *img = dev->get_image(tex->img);

				GLenum filter = tex->filter == ev2::FILTER_BILINEAR ? GL_LINEAR : GL_NEAREST;

				glBindTextureUnit(index, img->id);
				glBindTexture(GL_TEXTURE_2D, img->id);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
				break;
			}
			case ev2::DESCRIPTOR_TYPE_STORAGE_IMAGE: {
				if (EV2_IS_NULL(binding.img.handle))
					continue;
				Image *img = dev->get_image(binding.img.handle);

				GLenum format = image_format_to_gl_internal(img->fmt);

				glBindImageTexture(
					index, 
					img->id, 
					0, 
					GL_FALSE, 
					0, 
					GL_READ_WRITE,
					format
				);
				break;
			}
			case ev2::DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
			case ev2::DESCRIPTOR_TYPE_STORAGE_BUFFER: {
				if (EV2_IS_NULL(binding.buf.handle))
					continue;

				Buffer *buf = dev->get_buffer(binding.buf.handle);
				glBindBufferRange(GL_SHADER_STORAGE_BUFFER, index, buf->id, 
					  binding.buf.offset, binding.buf.size); 

				break;
			}

			case ev2::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
			case ev2::DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
				if (EV2_IS_NULL(binding.buf.handle))
					continue;

				Buffer *buf = dev->get_buffer(binding.buf.handle);
				glBindBufferRange(GL_UNIFORM_BUFFER, index, buf->id, 
					  binding.buf.offset, binding.buf.size); 
				break;
			}
		}
	}
}

void cmd_bind_compute_pipeline(RecorderID rec, ComputePipelineID h)
{
	ev2::Device *dev = EV2_TYPE_PTR_CAST(Device, rec);
	ComputePipeline *pipeline = dev->get_compute_pipeline(h);
	glUseProgram(pipeline->program);
}

void cmd_dispatch(
	RecorderID rec,
	uint32_t countx, 
	uint32_t county, 
	uint32_t countz
)
{
	ev2::Device *dev = EV2_TYPE_PTR_CAST(Device, rec);
	glDispatchCompute(countx, county, countz);
}

void cmd_use_buffer(
	RecorderID rec,
	BufferID buf,
	Usage usage
)
{
}

void cmd_use_texture(
	RecorderID rec,
	TextureID tex,
	Usage usage
)
{
}

void cmd_execute(RecorderID rec, SyncID sync)
{
}


};

//------------------------------------------------------------------------------
// For later

struct Binding 
{
	std::string name;
	std::string type;
	std::string path;
};

struct ShaderPipeline
{
	std::string frag;
	std::string vert;
};

struct PreMaterialInfo
{
	std::string name;
	ShaderPipeline pipeline;
	std::vector<Binding> bindings;
};

static ev2::Result parse_material_file(
	PreMaterialInfo *info, std::string_view path)
{
	YAML::Node root = YAML::LoadFile(path.data());

	const YAML::Node &shaders_node = root["shaders"];

	if (!shaders_node) {
		log_error("Material %s does not specify any shaders!",path);
		return ev2::ELOAD_FAILED;
	}

	if (!shaders_node.IsMap()) {
		log_error("'shaders' field is not a map!");
		return ev2::ELOAD_FAILED;
	}

	ShaderPipeline pipeline;	

	const YAML::Node &vert = shaders_node["vert"];
	const YAML::Node &frag = shaders_node["frag"];

	if (!frag) {
		log_error("Material %s does not contain a fragment shader",path);
		return ev2::ELOAD_FAILED;
	}

	if (!vert) {
		log_error("Material %s does not contain a vertex shader", path);
		return ev2::ELOAD_FAILED;
	}

	pipeline.frag = frag.as<std::string>();
	pipeline.vert = vert.as<std::string>();

	YAML::Node bindings_node = root["bindings"];

	if (bindings_node && !bindings_node.IsSequence()) {
		log_error("'bindings' node is not a sequence!");
		return ev2::ELOAD_FAILED;
	}

	std::vector<Binding> bindings;

	if (bindings_node) {
		for (const auto& node : bindings_node) {
			YAML::Node path = node["path"]; 
			Binding b;
			b.path = path.IsDefined() ? path.as<std::string>() : "";
			b.type = node["type"].as<std::string>();
			b.name = node["name"].as<std::string>();
			bindings.push_back(std::move(b));
		}
	}

	*info = {
		.name = std::string(path),
		.pipeline = std::move(pipeline),
		.bindings = std::move(bindings)
	};

	return ev2::SUCCESS;
}
/*

static ev2::DescriptorSetID initialize_material(
	ev2::Device *dev,
	ev2::DescriptorLayoutID layout, 
	const PreMaterialInfo *info)
{
	size_t bind_count = info->bindings.size();
	if (bind_count) {

		std::vector<ev2::TextureID> textures (bind_count,EV2_NULL_HANDLE);

		const char* material_name = "material";

		for (size_t i = 0; i < info->bindings.size(); ++i) {
			const Binding *bind = &info->bindings[i];

			ev2::ImageAssetID texID = bind->path.empty() ? 
				EV2_NULL_HANDLE : ev2::load_texture_asset(dev, bind->path.c_str());

			if (!bind->path.empty() && texID == RESOURCE_HANDLE_NULL) {
				log_error(
					"While loading material %s : failed to load texture %s for binding=%s\n",
					info->name.c_str(),bind->path.c_str(),bind->name.c_str());
				continue;
			}

			// TODO : Create texture objects based on sampler in shader

			textures[i] = tex;
		}

		ev2::DescriptorSetID set = ev2::create_descriptor_set(
			dev, EV2_HANDLE_CAST(DescriptorLayout, layout));

		for (size_t i = 0; i < bind_count; ++i) {
			ev2::TextureID texID = textures[i];
			
			const Binding *bind = &info->bindings[i];

			ev2::DescriptorSlot slot = ev2::find_descriptor(layout, bind->name.c_str());
			ev2::bind_set_texture(dev, set, slot, texID);
		}

		return set;
	}

	log_info("Loaded material : %s",info->name.c_str());
}
*/
