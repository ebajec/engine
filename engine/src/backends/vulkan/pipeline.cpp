#include <ev2/pipeline.h>
#include <ev2/utils/ansi_colors.h>
#include <ev2/utils/log.h>

#include "vulkan/vulkan_core.h"

#include "backends/vulkan/def_vulkan.h"
#include "backends/vulkan/context_impl.h"
#include "backends/vulkan/pipeline_impl.h"
#include "backends/vulkan/resource_impl.h"

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

static inline bool _check_or_log(const char *s){
	if (s)
		log_error("%s is false",s);
	return !s;
}
#define check_or_log(cond) _check_or_log((cond) ? nullptr : #cond)

static int read_spv_file(const fs::path& path, std::vector<uint32_t> &data)
{
	size_t size = fs::file_size(path);

	if (size % sizeof(uint32_t) != 0) {
		log_error("spir-v file size is not aligned to 4 bytes");
		return -1;
	}

	data.resize(size/sizeof(uint32_t));

	std::ifstream file = std::ifstream(path, std::ios::binary);

	if (!file) {
		log_error("failed to read spir-v");
		return -1;
	}

	file.read(reinterpret_cast<char*>(data.data()), (intptr_t)size);

	return 0;
}

std::string get_layout_string(const ev2::ShaderLayout& layout)
{
	std::string info;
	for (const auto [name, entry] : layout.bindings) {
		info += "\t(set " + std::to_string(entry.set) 
			+ ", index " + std::to_string(entry.binding.binding) + ") : " 
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
	info += get_layout_string(shader->layout);

	return info;

}


//------------------------------------------------------------------------------
// NEW GOOD

ev2::Result get_shader_stage_from_path(const fs::path &path, 
									   ev2::ShaderStage *stage)
{
	std::string filestr = path.string(); 

	if (!fs::is_regular_file(path)) {
		log_error("File does not exist: %s", filestr.c_str());
		return ev2::ELOAD_FAILED;
	}

	std::string ext = path.extension().string();

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

	if (ext == ".comp")
		*stage = ev2::STAGE_COMPUTE;
	else if (ext == ".vert")
		*stage = ev2::STAGE_VERTEX;
	else if (ext == ".frag")
		*stage = ev2::STAGE_FRAGMENT;
}

VkShaderStageFlags get_vk_shader_stage_flags(ev2::ShaderStage stage)
{
	switch (stage) {
		case ev2::STAGE_COMPUTE:
			return VK_SHADER_STAGE_COMPUTE_BIT;
		case ev2::STAGE_FRAGMENT:
			return VK_SHADER_STAGE_FRAGMENT_BIT;
		case ev2::STAGE_VERTEX:
			return VK_SHADER_STAGE_VERTEX_BIT;
		default:
			return 0;
	}
}

static ev2::Result initialize_shader_bindings(ev2::Shader *shader, 
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

		if (binding->set < ev2::EV2_RESERVED_DESCRIPTOR_SET_MAX) {
			continue;
		}

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

		uint32_t descriptor_count = 1; 

		for (uint32_t i = 0; i < binding->array.dims_count; ++i) {
			descriptor_count *= binding->array.dims[i];
		}

		std::vector<VkDescriptorSetLayoutBinding> &layout_bindings = 
			shader->layout.bindings[binding->set];

		shader->layout.binding_names[name] = {
			.set = binding->set,
			.idx = layout_bindings.size()
		};

		layout_bindings.push_back(VkDescriptorSetLayoutBinding{
			.binding = binding->binding,
			.descriptorType = (VkDescriptorType)binding->descriptor_type,
			.descriptorCount = descriptor_count,
			.stageFlags = get_vk_shader_stage_flags(shader->stage),
			.pImmutableSamplers = nullptr,
		});		
	}

	spvReflectDestroyShaderModule(&module);

	return result == SPV_REFLECT_RESULT_SUCCESS ? ev2::SUCCESS : ev2::ELOAD_FAILED;
}


static ev2::Result load_shader_file2(ev2::GfxContext *ctx, const char *path, ev2::Shader* out)
{
	fs::path file (path);

	std::string filestr = file.string(); 

	if (!fs::is_regular_file(file)) {
		log_error("File does not exist: %s", filestr.c_str());
		return ev2::ELOAD_FAILED;
	}

	ev2::ShaderStage stage;
	ev2::Result result = get_shader_stage_from_path(file, &stage);

	// not a shader, ignore
	if (!stage)
		return ev2::ELOAD_FAILED;

	std::vector<uint32_t> code;
	if (read_spv_file(file,code) < 0) 
		return ev2::ELOAD_FAILED;

	ev2::Shader shader {
		.stage = stage,
	};

	VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VkResult vk_res = vkCreateShaderModule(ctx->device, &createInfo, 
										nullptr, &shader.shader_module);
    if (vk_res != VK_SUCCESS) {
        log_error("failed to create shader module!");
		return ev2::ELOAD_FAILED;
    }

	result = initialize_shader_bindings(&shader, code.data(), code.size()*sizeof(uint32_t));

	if (result != ev2::SUCCESS)
		return result;

	*out = std::move(shader);

	return result;
}


ev2::Result shader_create_callback(ev2::GfxContext *ctx, ev2::Shader *p_shader, const char *path)
{
	std::string syspath = ctx->assets->get_system_path(path);
	ev2::Result res = load_shader_file2(ctx, syspath.c_str(), p_shader);

	if (res != ev2::SUCCESS)
		return res;

	std::string info_str = get_shader_info(p_shader);
	log_info("Shader : "COLORIZE_PATH(%s)"\n%s",path, info_str.c_str());

	return res;
}

void shader_destroy_callback(ev2::GfxContext *ctx, void *usr)
{
	if (!usr) 
		return;

	ev2::Shader *shader = static_cast<ev2::Shader*>(usr);

	if (shader->shader_module)
		vkDestroyShaderModule(ctx->device, shader->shader_module, nullptr);

	delete shader;
}

ev2::Result shader_reload_callback(ev2::GfxContext *ctx, void **usr, const char *path)
{
	ev2::Shader new_shader{};

	ev2::Result res = shader_create_callback(ctx, &new_shader, path);

	if (res != ev2::SUCCESS)
		return res;
	
	ev2::Shader *shader = static_cast<ev2::Shader*>(*usr);

	if (shader->shader_module)
		vkDestroyShaderModule(ctx->device, shader->shader_module, nullptr);

	*shader = std::move(new_shader);
	return res;
}

//------------------------------------------------------------------------------
// GfxPipeline

struct GfxPipelineInfo
{
	std::string vert_path; 
	std::string frag_path; 
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

	info->frag_path = frag.as<std::string>();
	info->vert_path = vert.as<std::string>();

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

ev2::Result merge_shader_layouts(
	const ev2::ShaderLayout &a, 
	const ev2::ShaderLayout &b,
	ev2::ShaderLayout &out
) 
{
	out.bindings = a.bindings;
	out.binding_names = a.binding_names;

	ev2::Result result = ev2::SUCCESS;

	// this is disgusting

	for (const auto &[b_name, b_ent] : b.binding_names) {

		const VkDescriptorSetLayoutBinding &b_layout_binding = 
			b.bindings.at(b_ent.set)[b_ent.idx];

		auto it = out.binding_names.find(b_name);
		if (it != out.binding_names.end()) {
			ev2::ShaderLayout::BindingEntry &a_ent = it->second;

			if (check_or_log(a_ent.set == b_ent.set)) {
				return ev2::EMISMATCHED_SHADERS;
			}

			VkDescriptorSetLayoutBinding &a_layout_binding = out.bindings[a_ent.set][a_ent.idx];

			a_layout_binding.stageFlags |= b_layout_binding.stageFlags;

			if (check_or_log(a_layout_binding.binding == b_layout_binding.binding)) {
				return ev2::EMISMATCHED_SHADERS;
			}
			if (check_or_log(a_layout_binding.descriptorType == b_layout_binding.descriptorType)) {
				return ev2::EMISMATCHED_SHADERS;
			}
			if (check_or_log(a_layout_binding.descriptorCount == b_layout_binding.descriptorCount)) {
				return ev2::EMISMATCHED_SHADERS;
			}
		} else {
			auto it = out.bindings.emplace(b_ent.set, std::vector<VkDescriptorSetLayoutBinding>{});
			it.first->second.push_back(b_layout_binding);

			out.binding_names[b_name] = {
				.set = b_ent.set,
				.idx = it.first->second.size()
			};
		}
	}

	return result;
}

ev2::Result generate_shader_descriptor_layouts(
	ev2::GfxContext *ctx,
	const ev2::ShaderLayout &shader_layout,
	std::unordered_map<uint32_t, VkDescriptorSetLayout> &layouts
)
{
	layouts.reserve(shader_layout.bindings.size());

	for (const auto &[set, layout_bindings] : shader_layout.bindings) {
		VkDescriptorSetLayoutCreateInfo create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.flags = 0,
			.bindingCount = (uint32_t)layout_bindings.size(),
			.pBindings = layout_bindings.data(),
		};

	  	VkDescriptorSetLayout layout;
		VkResult result = vkCreateDescriptorSetLayout(ctx->device, &create_info, nullptr, &layout);

		layouts[set] = layout;

		if (result != VK_SUCCESS)
			return ev2::ELOAD_FAILED;
	}

	return ev2::SUCCESS;
}

ev2::Result initialize_gfx_pipeline_layout(
	ev2::GfxContext *ctx, const GfxPipelineInfo *info, ev2::GfxPipeline *pipeline)
{
	const ev2::Shader *vert = ctx->get_shader(pipeline->vert);
	const ev2::Shader *frag = ctx->get_shader(pipeline->frag);

	ev2::ShaderLayout merged_layout {};

	ev2::Result result = merge_shader_layouts(vert->layout, frag->layout, merged_layout);

	if (result)
		return result;

	VkPipelineLayoutCreateInfo layout_info = {
    	.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    	.setLayoutCount = 1,
    	.pSetLayouts =  &descriptorSetLayout,
    	.pushConstantRangeCount = 0,
    	.pPushConstantRanges = nullptr,
	};

	VkResult vk_res = 
		vkCreatePipelineLayout(ctx->device, &layout_info, nullptr, &pipeline->layout); 

    if (vk_res != VK_SUCCESS) {
        log_error("failed to create graphics pipeline layout!");
		return ev2::ELOAD_FAILED;
    }

}

ev2::Result initialize_gfx_pipeline_vk_pipeline(
	ev2::GfxContext *ctx, const GfxPipelineInfo *info, ev2::GfxPipeline *pipeline)
{
	const ev2::Shader *vert = ctx->get_shader(pipeline->vert);
	const ev2::Shader *frag = ctx->get_shader(pipeline->frag);

	VertexInputLayout vertex_layout {};

	std::string sys_vert_path = ctx->assets->get_system_path(info->vert_path.c_str());
	ev2::Result result = parse_vertex_layout(sys_vert_path.c_str(), &vertex_layout);

	if (result)
		return result;

}

static ev2::Result initialize_gfx_pipeline(
	ev2::GfxContext *ctx, 
	const GfxPipelineInfo *info,
	ev2::GfxPipeline *out
) 
{
	ev2::Result result = ev2::SUCCESS;

	ev2::ShaderID vert_handle = ev2::load_shader(ctx, info->vert_path.c_str());
	if (!vert_handle.id) {
		result = ev2::EUNKNOWN;
		return result;
	}

	ev2::ShaderID frag_handle = ev2::load_shader(ctx, info->frag_path.c_str());
	if (!frag_handle.id) {
		result = ev2::EUNKNOWN;
		return result;
	}

	ev2::GfxPipeline pipeline = {
		.frag = frag_handle,
		.vert = vert_handle,
	};

	result = initialize_gfx_pipeline_layout(ctx, info, &pipeline);

	if (result)
		return result;

	result = initialize_gfx_pipeline_vk_pipeline(ctx, info, &pipeline);

	if (result)
		return result;

	*out = std::move(pipeline);

	return result;
}

std::string get_gfx_pipeline_info(ev2::GfxContext *ctx, ev2::GfxPipeline *p_pipeline)
{
	const char *vert = ctx->assets->get_entry((AssetID)p_pipeline->vert.id)->path;
	const char *frag = ctx->assets->get_entry((AssetID)p_pipeline->frag.id)->path;

	const char *fmt = 
		"\tvert: "COLORIZE_PATH(%s)"\n"
		"\tfrag: "COLORIZE_PATH(%s)"";

	std::string buf;
	buf.resize((strlen(fmt) + strlen(vert) + strlen(frag)) + 1);
	
	snprintf(buf.data(), buf.size(), fmt, vert, frag);
	return buf;
}

static ev2::Result gfx_pipeline_create_callback(
	ev2::GfxContext *ctx, 
	ev2::GfxPipeline *p_pipeline, 
	const char *path
)
{
	ev2::Result result = ev2::SUCCESS;
	std::string syspath = ctx->assets->get_system_path(path);

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

	result = initialize_gfx_pipeline(
		ctx, 
		&gfx_info,
		p_pipeline 
	);

	if (result) {
		log_error("Failed to initialize shaders for %s", path);
		return result;
	}

	std::string info = get_gfx_pipeline_info(ctx, p_pipeline);
	log_info("Graphics pipeline : "COLORIZE_PATH(%s)"\n%s", path, info.c_str());

	return result;
}

static void destroy_gfx_pipeline(ev2::GfxContext *ctx, ev2::GfxPipeline *pipeline)
{
	if (pipeline->layout)
		vkDestroyPipelineLayout(ctx->device, pipeline->layout, nullptr);
	if (pipeline->pipeline)
		vkDestroyPipeline(ctx->device, pipeline->pipeline, nullptr);
}

static void gfx_pipeline_destroy_callback(ev2::GfxContext *ctx, void* usr) 
{
	ev2::GfxPipeline *pipeline = static_cast<ev2::GfxPipeline*>(usr);

	destroy_gfx_pipeline(ctx, pipeline);

	ev2::unload_shader(ctx, pipeline->vert);
	ev2::unload_shader(ctx, pipeline->frag);

	delete pipeline;
}

static ev2::Result gfx_pipeline_reload_callback(ev2::GfxContext *ctx, void** usr, const char *path)
{
	ev2::GfxPipeline new_pipeline{};
	ev2::Result res = gfx_pipeline_create_callback(ctx, &new_pipeline, path);

	if (res != ev2::SUCCESS)
		return res;

	ev2::GfxPipeline *pipeline = static_cast<ev2::GfxPipeline*>(*usr);

	destroy_gfx_pipeline(ctx, pipeline);

	*pipeline = std::move(new_pipeline);
	return res;
}

//------------------------------------------------------------------------------
// Compute shaders

static ev2::Result gl_compute_pipeline_create(
	ev2::GfxContext *ctx, 
	ev2::ComputePipeline *p_pipeline, 
	const char *path
)
{
	ev2::Shader *p_shader = &p_pipeline->shader;
	ev2::Result result = shader_create_callback(ctx, p_shader, path);

	if (result)
		return result;

	if (p_shader->stage != ev2::STAGE_COMPUTE) {
		shader_destroy_callback(ctx, p_shader);
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

static void gl_compute_pipeline_destroy(ev2::GfxContext *ctx, void* usr) 
{
	ev2::ComputePipeline *pipeline = static_cast<ev2::ComputePipeline*>(usr);

	if (pipeline->program)
		glDeleteProgram(pipeline->program);

	glDeleteShader(pipeline->shader.id);

	delete pipeline;
}

static ev2::Result gl_compute_pipeline_reload(ev2::GfxContext *ctx, void** usr, const char *path)
{
	ev2::ComputePipeline new_pipeline{};

	ev2::Result res = gl_compute_pipeline_create(ctx, &new_pipeline, path);

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

ShaderID load_shader(GfxContext *ctx, const char *path)
{
	AssetID id = ctx->assets->load(path);
	if (id)
		return ShaderID{id};

	static AssetVTable vtbl = {
		.reload = shader_reload_callback,
		.destroy = shader_destroy_callback,
	};

	ev2::Shader *shader = new ev2::Shader{}; 

	ev2::Result res = shader_create_callback(ctx, shader, path);

	if (res == ev2::SUCCESS) {
		id = ctx->assets->allocate(&vtbl, shader, path); 
		return ShaderID{id};
	}

	delete shader;

	return EV2_NULL_HANDLE(Shader);
}

void unload_shader(GfxContext *ctx, ShaderID shader)
{
}

//------------------------------------------------------------------------------

GfxPipelineID load_graphics_pipeline(GfxContext *ctx, const char *path)
{
	AssetID id = ctx->assets->load(path);

	if (id)
		return GfxPipelineID{id};

	static AssetVTable vtbl = {
		.reload = gfx_pipeline_reload_callback,
		.destroy = gfx_pipeline_destroy_callback,
	};

	ev2::GfxPipeline *pipeline = new ev2::GfxPipeline{}; 
	ev2::Result res = gfx_pipeline_create_callback(ctx, pipeline, path);

	if (res == ev2::SUCCESS) {
		id = ctx->assets->allocate(&vtbl, pipeline, path);

		if (ctx->assets->reloader) {
			ctx->assets->reloader->add_dependency(pipeline->vert.id, id);
			ctx->assets->reloader->add_dependency(pipeline->frag.id, id);
		}

		return GfxPipelineID{id};
	}

	delete pipeline;
	return EV2_NULL_HANDLE(GraphicsPipeline);
}

void unload_graphics_pipeline(GfxContext *ctx, GfxPipelineID pipe)
{
}

//------------------------------------------------------------------------------

ComputePipelineID load_compute_pipeline(GfxContext *ctx, const char *path)
{
	AssetID id = ctx->assets->load(path);

	if (id)
		return ComputePipelineID{id};

	static AssetVTable vtbl = {
		.reload = gl_compute_pipeline_reload,
		.destroy = gl_compute_pipeline_destroy,
	};

	ev2::ComputePipeline *pipeline = new ev2::ComputePipeline{}; 
	ev2::Result res = gl_compute_pipeline_create(ctx, pipeline, path);

	if (res == ev2::SUCCESS) {
		id = ctx->assets->allocate(&vtbl, pipeline, path);
		return ComputePipelineID{id};
	}

	delete pipeline;
	return EV2_NULL_HANDLE(ComputePipeline);
}

void unload_compute_pipeline(GfxContext *ctx, ComputePipelineID pipe)
{
}

//------------------------------------------------------------------------------

DescriptorLayoutID get_graphics_pipeline_layout(GfxContext *ctx, GfxPipelineID pipe)
{
	const ev2::GfxPipeline * res = ctx->assets->get<ev2::GfxPipeline>(pipe.id);

	if (!res)
		return EV2_NULL_HANDLE(DescriptorLayout);

	const DescriptorLayout *p_layout = &res->layout;
	return DescriptorLayoutID{.id = reinterpret_cast<uint64_t>(&res->layout)};
}

DescriptorLayoutID get_compute_pipeline_layout(GfxContext *ctx, ComputePipelineID pipe)
{
	if (!pipe.id)
		return EV2_NULL_HANDLE(DescriptorLayout);

	const ev2::ComputePipeline * res = ctx->assets->get<ev2::ComputePipeline>(pipe.id);
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
	GfxContext *ctx, 
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

void destroy_descriptor_set(GfxContext *ctx, DescriptorSetID id)
{
	DescriptorSet *set = EV2_TYPE_PTR_CAST(DescriptorSet, id);

	delete set;
}

ev2::Result bind_buffer(
	GfxContext *ctx, 
	DescriptorSetID set_id, 
	BindingSlot slot, 
	BufferID buf_id, 
	size_t offset, 
	size_t size
) 
{
	DescriptorSet *set = EV2_TYPE_PTR_CAST(DescriptorSet, set_id);
	//ev2::Buffer *buf = ctx->rt->get<ev2::Buffer>(buf_id);

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
	GfxContext *ctx, 
	DescriptorSetID set_id, 
	BindingSlot slot, 
	TextureID tex_id  
) 
{
	DescriptorSet *set = EV2_TYPE_PTR_CAST(DescriptorSet, set_id);
	Texture *tex = ctx->get_texture(tex_id);

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
	GfxContext *ctx,
	DescriptorSetID h_set, 
	BindingSlot slot, 
	ImageID h_img
)
{
	DescriptorSet *set = EV2_TYPE_PTR_CAST(DescriptorSet, h_set);
	Image *img = ctx->get_image(h_img);

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
// Internal interface

VkDescriptorSetLayout generate_per_frame_descriptor_set_layout(ev2::GfxContext *ctx)
{
	VkDescriptorSetLayoutBinding bindings[] = {
		// Frame global ubo
		VkDescriptorSetLayoutBinding{
			.binding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.stageFlags = VK_SHADER_STAGE_ALL,
		}
	};

	VkDescriptorSetLayoutCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = 0,
		.pBindings = bindings,
		.bindingCount = sizeof(bindings)/sizeof(VkDescriptorSetLayoutBinding),
	};
}
VkDescriptorSetLayout generate_per_pass_descriptor_set_layout(ev2::GfxContext *ctx)
{
	VkDescriptorSetLayoutBinding bindings[] = {
		VkDescriptorSetLayoutBinding{
			.binding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.stageFlags = VK_SHADER_STAGE_ALL,
		}
	};

	VkDescriptorSetLayoutCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = 0,
		.pBindings = bindings,
		.bindingCount = sizeof(bindings)/sizeof(VkDescriptorSetLayoutBinding),
	};
}
VkDescriptorSetLayout generate_bindless_descriptor_set_layout(ev2::GfxContext *ctx)
{
	VkDescriptorSetLayoutBinding bindings[] = {
	};

	VkDescriptorSetLayoutCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = 0,
		.pBindings = bindings,
		.bindingCount = sizeof(bindings)/sizeof(VkDescriptorSetLayoutBinding),
	};
}

//------------------------------------------------------------------------------
// Interface

RecorderID begin_commands(GfxContext * ctx, CommandMode mode)
{
	return EV2_HANDLE_CAST(Recorder, ctx);
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
	GfxContext *ctx = EV2_TYPE_PTR_CAST(GfxContext, rec);
	DescriptorSet *set = EV2_TYPE_PTR_CAST(DescriptorSet, set_id);

	for (auto [index, binding] : set->bindings) {
		switch(binding.type) {
			case ev2::DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			case ev2::DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			case ev2::DESCRIPTOR_TYPE_SAMPLER: {
				if (EV2_IS_NULL(binding.tex.handle))
					continue;

				Texture *tex = ctx->get_texture(binding.tex.handle);
				Image *img = ctx->get_image(tex->img);

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
				Image *img = ctx->get_image(binding.img.handle);

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

				Buffer *buf = ctx->get_buffer(binding.buf.handle);
				glBindBufferRange(GL_SHADER_STORAGE_BUFFER, index, buf->id, 
					  binding.buf.offset, binding.buf.size); 

				break;
			}

			case ev2::DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
			case ev2::DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
				if (EV2_IS_NULL(binding.buf.handle))
					continue;

				Buffer *buf = ctx->get_buffer(binding.buf.handle);
				glBindBufferRange(GL_UNIFORM_BUFFER, index, buf->id, 
					  binding.buf.offset, binding.buf.size); 
				break;
			}
		}
	}
}

void cmd_bind_compute_pipeline(RecorderID rec, ComputePipelineID h)
{
	ev2::GfxContext *ctx = EV2_TYPE_PTR_CAST(GfxContext, rec);
	ComputePipeline *pipeline = ctx->get_compute_pipeline(h);
	glUseProgram(pipeline->program);
}

void cmd_dispatch(
	RecorderID rec,
	uint32_t countx, 
	uint32_t county, 
	uint32_t countz
)
{
	ev2::GfxContext *ctx = EV2_TYPE_PTR_CAST(GfxContext, rec);
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
