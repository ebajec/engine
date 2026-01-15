#include "ev2/pipeline.h"

#include "device_impl.h"
#include "pipeline_impl.h"
#include "resource_impl.h"

#include "resource/gl_utils.h"

#include "utils/log.h"

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

static int spv_load(const fs::path& path, std::vector<uint32_t> &data)
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

	// TODO: not pretty, but it works for now

	std::string info;

	info += "\tstage : " + std::string(stage) + "\n";

	for (const auto [name, binding] : shader->layout->bindings) {
		info += "\t(set " + std::to_string(binding.set) 
			+ ", index " + std::to_string(binding.id) + ") : " 
			 + name + "\n";	
	}
	if (!info.empty())
		info.erase(info.size() - 1);

	return info;
}

static ev2::Result load_shader_file(const char *path, ev2::Shader* out)
{
	fs::path file (path);

	std::string filestr = file.string(); 

	if (!fs::is_regular_file(file)) {
		return ev2::ELOAD_FAILED;
	}

	std::string name = file.filename().string();
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

		name.resize(name.size() - sizeof(".spv") + 1);
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
	if (spv_load(file,code) < 0) 
		return ev2::ELOAD_FAILED;

	auto layout = std::make_unique<ev2::DescriptorLayout>();

	ev2::Result res = get_spv_module_layout(
		layout.get(), code.data(), code.size()*sizeof(uint32_t));

	GLuint id = compile_spv_gl(stage, code.data(), code.size());

	if (!id) {
		log_error("While loading shader '%s' , failed to compile '%s'", 
			name.c_str(), filestr.c_str());
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

	std::string info_str = get_shader_info(out);

	log_info("Loaded shader : %s\n%s",name.c_str(), info_str.c_str());

	return res;
}

ev2::Result gl_shader_create(ev2::Device *dev, ev2::Shader *p_shader, const char *path)
{
	ev2::Result res = load_shader_file(path, p_shader);
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

static ev2::Result gl_gfx_pipeline_create(
	ev2::Device *dev, 
	ev2::GraphicsPipeline *p_pipeline, 
	const char *path
)
{
	ev2::Result result = ev2::SUCCESS;

	GfxPipelineInfo gfx_info;
	try {
		result = parse_gfx_pipeline_file(&gfx_info,path);
	} catch (YAML::Exception e) {
		log_error("Error while parsing YAML: %s",e.what());
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

	if (result)
		return result;

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
	ev2::Result result = ev2::SUCCESS;

	std::string realpath = dev->assets->get_system_path(path);

	ev2::ShaderID handle = ev2::load_shader(dev, realpath.c_str());

	if (!handle.id) {
		result = ev2::ELOAD_FAILED;
		return result;
	}

	ev2::Shader *comp = dev->assets->get<ev2::Shader>(handle.id);

	if (comp->stage != ev2::STAGE_COMPUTE) {
		ev2::unload_shader(dev, handle);
		return ev2::ELOAD_FAILED;
	}

	GLuint program = glCreateProgram();
	glAttachShader(program, comp->id);
	glLinkProgram(program);

	if (!gl_check_program(program, path)) {
		glDeleteProgram(program);
		return ev2::ELOAD_FAILED;
	}

	p_pipeline->program = program;
	p_pipeline->comp = handle;
	p_pipeline->layout = *comp->layout;

	return result;
}

static void gl_compute_pipeline_destroy(ev2::Device *dev, void* usr) 
{
	ev2::ComputePipeline *pipeline = static_cast<ev2::ComputePipeline*>(usr);

	if (pipeline->program)
		glDeleteProgram(pipeline->program);

	ev2::unload_shader(dev, pipeline->comp);

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

	std::string realpath = dev->assets->get_system_path(path);
	ev2::Result res = gl_shader_create(dev, shader, realpath.c_str());

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

	std::string realpath = dev->assets->get_system_path(path);

	ev2::GraphicsPipeline *pipeline = new ev2::GraphicsPipeline{}; 
	ev2::Result res = gl_gfx_pipeline_create(dev, pipeline, realpath.c_str());

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
		if (dev->assets->reloader)
			dev->assets->reloader->add_dependency(pipeline->comp.id, id);
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
	const DescriptorLayout *p_layout = &res->layout;
	return DescriptorLayoutID{.id = reinterpret_cast<uint64_t>(&res->layout)};
}

DescriptorLayoutID get_compute_pipeline_layout(Device *dev, ComputePipelineID pipe)
{
	if (!pipe.id)
		return EV2_NULL_HANDLE(DescriptorLayout);

	const ev2::ComputePipeline * res = dev->assets->get<ev2::ComputePipeline>(pipe.id);
	const DescriptorLayout *p_layout = &res->layout;

	return DescriptorLayoutID{.id = reinterpret_cast<uint64_t>(&res->layout)};
}

DescriptorSlot find_descriptor(DescriptorLayoutID id, const char *name)
{
	DescriptorLayout *layout = EV2_TYPE_PTR_CAST(DescriptorLayout, id);

	auto it = layout->bindings.find(name);

	if (it != layout->bindings.end()) {
		return {.set = it->second.set, .id = it->second.id};
	}
	return {UINT16_MAX,UINT16_MAX};
}

DescriptorSetID create_descriptor_set(
	Device *dev, 
	DescriptorLayoutID layout_id, 
	uint16_t index
)
{
	DescriptorLayout * layout = EV2_TYPE_PTR_CAST(DescriptorLayout, layout_id);
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

void descriptor_set_bind_buffer(
	Device *dev, 
	DescriptorSetID set_id, 
	DescriptorSlot slot, 
	BufferID buf_id, 
	size_t offset, 
	size_t size
) 
{
	DescriptorSet *set = EV2_TYPE_PTR_CAST(DescriptorSet, set_id);
	//ev2::Buffer *buf = dev->rt->get<ev2::Buffer>(buf_id);

	if (set->index != slot.set) {
		log_error(
			"Mismatched binding for buffer %d. (set=%d, index=%d) to set %d",
			buf_id, slot.set, slot.id, set->index
		);
		return;
	}

	auto it = set->bindings.find(slot.id);

	if (it == set->bindings.end()) {
		log_error(
			"Attempting to bind buffer %d to nonexistent index %d in set %d", 
			buf_id, slot.id, slot.set);
	}

	ResourceBinding *binding = &it->second;
	if (
		binding->type != DESCRIPTOR_TYPE_STORAGE_BUFFER && 
		binding->type != DESCRIPTOR_TYPE_UNIFORM_BUFFER) 
		return;

	binding->buf = buf_id;
}

void bind_set_texture(
	Device *dev, 
	DescriptorSetID set_id, 
	DescriptorSlot slot, 
	TextureID tex_id  
) 
{
	DescriptorSet *set = EV2_TYPE_PTR_CAST(DescriptorSet, set_id);
	Texture *tex = dev->get_buffer(tex_id);

	if (set->index != slot.set) {
		log_error(
			"Mismatched binding for image %d. (set=%d, index=%d) to set %d",
			tex->img, slot.set, slot.id, set->index
		);
		return;
	}

	auto it = set->bindings.find(slot.id);

	if (it == set->bindings.end()) {
		log_error(
			"Attempting to bind image %d to nonexistent index %d in set %d", 
			tex->img, slot.id, slot.set);
		return;
	}

	ResourceBinding *binding = &it->second;

	if (
		binding->type != DESCRIPTOR_TYPE_SAMPLER && 
		binding->type != DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && 
		binding->type != DESCRIPTOR_TYPE_STORAGE_IMAGE) {
		log_error("Attempting to bind invalid resource to texture");
		return;
	}

	binding->tex = tex_id;
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
		if (EV2_IS_NULL(binding.tex))
			continue;

		switch(binding.type) {
			case ev2::DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			case ev2::DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			case ev2::DESCRIPTOR_TYPE_SAMPLER:
				Texture *tex = dev->get_buffer(binding.tex);
				Image *img = dev->get_buffer(tex->img);
				glBindTextureUnit(index, img->id);
			break;
		}
	}
}

void cmd_dispatch(
	RecorderID rec,
	ComputePipelineID pipe, 
	uint32_t countx, 
	uint32_t county, 
	uint32_t countz
)
{
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
