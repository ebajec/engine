#include <ev2/pipeline.h>
#include <ev2/utils/ansi_colors.h>
#include <ev2/utils/log.h>

#include "vulkan/vulkan_core.h"

#include "backends/vulkan/def_vulkan.h"
#include "backends/vulkan/context.h"
#include "backends/vulkan/pipeline.h"
#include "backends/vulkan/resource.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include "spirv_reflect.h"
#pragma clang diagnostic pop

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
	return s;
}
#define check_or_log(cond) _check_or_log((cond) ? nullptr : #cond)

static ev2::Result read_spv_file(const fs::path& path, std::vector<uint32_t> &data)
{
	size_t size = fs::file_size(path);

	if (size % sizeof(uint32_t) != 0) {
		return set_error(ev2::ELOAD_FAILED, "spir-v file size is not aligned to 4 bytes");
	}

	data.resize(size/sizeof(uint32_t));

	std::ifstream file = std::ifstream(path, std::ios::binary);

	if (!file) {
		return set_error(ev2::ELOAD_FAILED, "failed to read spir-v");
	}

	file.read(reinterpret_cast<char*>(data.data()), (intptr_t)size);

	return ev2::SUCCESS;
}

static std::string get_layout_string(const ev2::ShaderLayoutMapping& layout)
{
	std::string info;
	for (const auto &[name, entry] : layout.binding_names) {
		auto it = layout.set_binding_infos.find(entry.set);
		info += "\t(set " + std::to_string(entry.set) 
			+ ", binding " + std::to_string(it->second.bindings[entry.idx].binding) + ") : " 
			 + name + "\n";	
	}
	if (!info.empty())
		info.erase(info.size() - 1);

	return info;
}

static std::string get_shader_info(ev2::Shader *shader)
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
	std::string layout_string = get_layout_string(*shader->layout_map); 
	info += layout_string.empty() ? "\t(no bindings)" : std::move(layout_string);

	return info;

}


//------------------------------------------------------------------------------
// NEW GOOD

static ev2::Result get_shader_stage_from_path(const fs::path &path, 
									   ev2::ShaderStage *stage)
{
	std::string filestr = path.string(); 

	if (!fs::is_regular_file(path)) {
		return set_error(ev2::ELOAD_FAILED, "File does not exist: %s", filestr.c_str());
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
			return set_error(ev2::ELOAD_FAILED, "Wrong extension");
		}

		size_t len = (size_t)(end - start);

		ext.resize(len);
		memcpy(ext.data(), start, len);
	} else {
		return set_error(ev2::ELOAD_FAILED, "Filename extension must be '.spv'");
	}

	if (ext == ".comp")
		*stage = ev2::STAGE_COMPUTE;
	else if (ext == ".vert")
		*stage = ev2::STAGE_VERTEX;
	else if (ext == ".frag")
		*stage = ev2::STAGE_FRAGMENT;
	else 
		return set_error(ev2::ELOAD_FAILED, "Invalid shader file extension");

	return ev2::SUCCESS;
}

static VkShaderStageFlags get_vk_shader_stage_flags(ev2::ShaderStage stage)
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

static ev2::Result parse_shader_bindings(
	ev2::Shader *shader,
	const SpvReflectShaderModule *p_module
)
{
	SpvReflectResult result = SPV_REFLECT_RESULT_SUCCESS;

	std::vector<SpvReflectDescriptorBinding*> bindings;
	uint32_t binding_count = 0;

	result = spvReflectEnumerateDescriptorBindings(
		p_module,&binding_count,nullptr); 
	bindings.resize(binding_count);
	if (result != SPV_REFLECT_RESULT_SUCCESS)
		return set_error(ev2::ELOAD_FAILED, "spvReflectEnumerateDescriptorBindings failed");

	result = spvReflectEnumerateDescriptorBindings(
		p_module,&binding_count,bindings.data()); 

	if (result != SPV_REFLECT_RESULT_SUCCESS)
		return set_error(ev2::ELOAD_FAILED, "spvReflectEnumerateDescriptorBindings failed");

	for (uint32_t i = 0; i < binding_count; ++i) {
		SpvReflectDescriptorBinding *binding = bindings[i];

		if (
			binding->set < EV2_BASE_SET_COUNT && 
			shader->stage != ev2::STAGE_COMPUTE
		) {
			shader->layout_map->set_binding_infos[binding->set] = {};
			continue;
		}

		std::string tmpname;

		const char *name = binding->name;

		const bool no_instance = !name || *name == '\0'; 

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

		const bool is_variable_sized = 
			binding->array.dims_count > 0 && 
			binding->array.dims[binding->array.dims_count - 1] == 0;

		uint32_t descriptor_count = 1; 

		if (is_variable_sized) {
			descriptor_count = 0;//EV2_MAX_BINDLESS_DESCRIPTORS;
		} else {
			for (uint32_t j = 0; j < binding->array.dims_count; ++j) {
				descriptor_count *= binding->array.dims[j];
			}
		}

		ev2::SetBindingInfo &binding_info = 
			shader->layout_map->set_binding_infos[binding->set]; 

		std::vector<VkDescriptorSetLayoutBinding> &layout_bindings = binding_info.bindings; 
		std::vector<ev2::ShaderBindingInfo> &layout_infos = binding_info.infos;

		shader->layout_map->binding_names[name] = {
			.set = binding->set,
			.idx = (uint32_t)layout_bindings.size(),
		};

		layout_bindings.push_back(VkDescriptorSetLayoutBinding{
			.binding = binding->binding,
			.descriptorType = (VkDescriptorType)binding->descriptor_type,
			.descriptorCount = descriptor_count,
			.stageFlags = get_vk_shader_stage_flags(shader->stage),
			.pImmutableSamplers = nullptr,
		});		

		layout_infos.push_back(ev2::ShaderBindingInfo{
			.is_variable_sized = is_variable_sized
		});
	}

	return ev2::SUCCESS;
}

static ev2::Result parse_shader_push_constants(
	ev2::Shader *shader, const SpvReflectShaderModule *p_module)
{
	uint32_t block_count;
	std::vector<SpvReflectBlockVariable*> blocks;

	spvReflectEnumeratePushConstantBlocks(p_module, &block_count, nullptr);
	blocks.resize(block_count);
	spvReflectEnumeratePushConstantBlocks(p_module, &block_count, blocks.data());

	std::vector<VkPushConstantRange> &ranges = shader->layout_map->push_constant_ranges;

	for (uint32_t i = 0; i < block_count; ++i) {
		const SpvReflectBlockVariable *block = blocks[i];

		VkPushConstantRange range = {
			.stageFlags = get_vk_shader_stage_flags(shader->stage),
			.offset = block->offset,
			.size = block->size,
		};

		ranges.push_back(range);
	}

	std::sort(ranges.begin(), ranges.end(),
		[](const VkPushConstantRange &r1, const VkPushConstantRange &r2) -> bool
		{
			return r1.offset < r2.offset;
		});

	return ev2::SUCCESS;
}

static ev2::Result parse_shader_reflection(ev2::Shader *shader, 
									  const void* code, size_t size)
{
	SpvReflectShaderModule module;

	SpvReflectResult spv_result = spvReflectCreateShaderModule(size, code, &module);
	
	if (spv_result != SPV_REFLECT_RESULT_SUCCESS)
		return set_error(ev2::ELOAD_FAILED, "spvReflectCreateShaderModule failed");

	ev2::Result result = parse_shader_bindings(shader, &module);

	if (result != ev2::SUCCESS)
		return result;

	result = parse_shader_push_constants(shader, &module);

	if (result != ev2::SUCCESS)
		return result;

	spvReflectDestroyShaderModule(&module);

	return result; 
}

static ev2::Result load_shader_file(ev2::GfxContext *ctx, const char *path, ev2::Shader* out)
{
	fs::path file (path);

	std::string filestr = file.string(); 

	if (!fs::is_regular_file(file)) {
		return set_error(
			ev2::ELOAD_FAILED,
			"File does not exist: %s", filestr.c_str());
	}

	ev2::ShaderStage stage;
	ev2::Result result = get_shader_stage_from_path(file, &stage);

	if (result)
		return result;

	std::vector<uint32_t> code;
	result = read_spv_file(file,code); 
	if (result) 
		return result;

	ev2::Shader shader {
		.stage = stage,
		.layout_map = std::make_shared<ev2::ShaderLayoutMapping>()
	};

	VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();

	VkResult vk_res = vkCreateShaderModule(ctx->device, &createInfo, 
										nullptr, &shader.shader_module);
    if (vk_res != VK_SUCCESS) {
        return set_error(ev2::ELOAD_FAILED, "failed to create shader module!");
    }

	result = parse_shader_reflection(&shader, code.data(), code.size()*sizeof(uint32_t));

	if (result != ev2::SUCCESS)
		return result;

	*out = std::move(shader);

	return result;
}


static ev2::Result shader_create_callback(ev2::GfxContext *ctx, ev2::Shader **pp_shader, const char *path)
{
	ev2::Shader *p_shader = new ev2::Shader{};

	std::string syspath = ctx->assets->get_system_path(path);
	ev2::Result res = load_shader_file(ctx, syspath.c_str(), p_shader);

	if (res != ev2::SUCCESS) {
		delete p_shader;
		return res;
	}

	std::string info_str = get_shader_info(p_shader);
	log_info("Shader : " COLORIZE_PATH(%s)"\n%s",path, info_str.c_str());

	*pp_shader = p_shader;

	return res;
}

static void shader_destroy_callback(ev2::GfxContext *ctx, void *usr)
{
	if (!usr) 
		return;

	ev2::Shader *shader = static_cast<ev2::Shader*>(usr);

	if (shader->shader_module)
		vkDestroyShaderModule(ctx->device, shader->shader_module, nullptr);

	delete shader;
}

static ev2::Result shader_reload_callback(ev2::GfxContext *ctx, void **usr, const char *path)
{
	ev2::Shader *new_shader;

	ev2::Result res = shader_create_callback(ctx, &new_shader, path);

	if (res != ev2::SUCCESS)
		return res;
	
	ev2::Shader *shader = static_cast<ev2::Shader*>(*usr);
	shader_destroy_callback(ctx, shader);

	*usr = new_shader;
	return res;
}

//------------------------------------------------------------------------------
// GfxPipeline

struct GfxPipelineInfo
{
	std::string vert_path; 
	std::string frag_path; 
};

static ev2::Result parse_gfx_pipeline_file(GfxPipelineInfo *info, const char *path)
{
	YAML::Node root = YAML::LoadFile(path);

	const YAML::Node &shaders_node = root["shaders"];

	if (!shaders_node) {
		return set_error(ev2::ELOAD_FAILED, 
				   "Pipeline %s does not specify any shaders!",path);
	}

	if (!shaders_node.IsMap()) {
		return set_error(ev2::ELOAD_FAILED, 
				   "'shaders' field is not a map!");
	}

	const YAML::Node &vert = shaders_node["vert"];
	const YAML::Node &frag = shaders_node["frag"];

	if (!frag) {
		return set_error(ev2::ELOAD_FAILED, 
				   "Pipeline %s does not contain a fragment shader",path);
	}

	if (!vert) {
		return set_error(ev2::ELOAD_FAILED, 
				   "Pipeline %s does not contain a vertex shader", path);
	}

	info->frag_path = frag.as<std::string>();
	info->vert_path = vert.as<std::string>();

	return ev2::SUCCESS;
}

static size_t spv_input_var_size(SpvReflectInterfaceVariable *var)
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
	std::vector<VkVertexInputBindingDescription> bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;
};

static ev2::Result parse_vertex_layout(const char *path, VertexInputLayout *p_out)
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
		return set_error(ev2::ELOAD_FAILED, "Failed to generate spv reflection");

	uint32_t in_count;
	spvReflectEnumerateInputVariables(&reflection, &in_count, nullptr);

	std::vector<SpvReflectInterfaceVariable*> vars (in_count);
	spvReflectEnumerateInputVariables(&reflection, &in_count, vars.data());

	std::sort(vars.begin(), vars.end(), [](
				const SpvReflectInterfaceVariable *a,
				const SpvReflectInterfaceVariable *b) {
				return a->location < b->location;
		   });

	size_t total_var_size = 0;

	//------------------------------------------------------------------------------
	// Attributes
	std::vector<VkVertexInputAttributeDescription> attributes;
	attributes.reserve(in_count);

	for (uint32_t i = 0; i < in_count; ++i) {
		SpvReflectInterfaceVariable* var = vars[i];
		if (var->built_in >= 0)
			continue;

		VkVertexInputAttributeDescription desc = {
			.location = var->location,
			.binding = 0,
			.format = (VkFormat)var->format,
			.offset = (uint32_t)total_var_size,
		};

		size_t size = spv_input_var_size(var);
		total_var_size += size;

		attributes.push_back(desc);
	}
	spvReflectDestroyShaderModule(&reflection);

	//------------------------------------------------------------------------------
	// Bindings
	
	// TODO: Will obviously need to have stride for not just
	// tightly packed data, and allow for more than just one vertex buffer
	std::vector<VkVertexInputBindingDescription> bindings = {
		VkVertexInputBindingDescription{
			.binding = 0,
			.stride = (uint32_t)total_var_size,
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		}
	};

	p_out->attributes = std::move(attributes);
	p_out->bindings = std::move(bindings);

	return ev2::SUCCESS;
}

static ev2::Result merge_shader_layouts(
	const ev2::ShaderLayoutMapping &a, 
	const ev2::ShaderLayoutMapping &b,
	ev2::ShaderLayoutMapping &out
) 
{
	out.set_binding_infos = a.set_binding_infos;
	out.binding_names = a.binding_names;

	ev2::Result result = ev2::SUCCESS;

	// this is disgusting

	for (const auto &[b_name, b_ent] : b.binding_names) {

		const VkDescriptorSetLayoutBinding &b_layout_binding = 
			b.set_binding_infos.at(b_ent.set).bindings[b_ent.idx];

		auto it = out.binding_names.find(b_name);
		if (it != out.binding_names.end()) {
			ev2::ShaderLayoutMapping::BindingEntry &a_ent = it->second;

			if (check_or_log(a_ent.set == b_ent.set)) {
				return ev2::EBAD_SHADER;
			}

			VkDescriptorSetLayoutBinding &a_layout_binding = 
				out.set_binding_infos[a_ent.set].bindings[a_ent.idx];

			a_layout_binding.stageFlags |= b_layout_binding.stageFlags;

			if (check_or_log(a_layout_binding.binding == b_layout_binding.binding)) {
				return ev2::EBAD_SHADER;
			}
			if (check_or_log(a_layout_binding.descriptorType == b_layout_binding.descriptorType)) {
				return ev2::EBAD_SHADER;
			}
			if (check_or_log(a_layout_binding.descriptorCount == b_layout_binding.descriptorCount)) {
				return ev2::EBAD_SHADER;
			}
		} else {
			auto it_binding = out.set_binding_infos.emplace(b_ent.set, std::vector<VkDescriptorSetLayoutBinding>{});
			it_binding.first->second.bindings.push_back(b_layout_binding);

			out.binding_names[b_name] = {
				.set = b_ent.set,
				.idx = (uint32_t)it_binding.first->second.bindings.size()
			};
		}
	}

	out.push_constant_ranges.reserve(
		a.push_constant_ranges.size() + b.push_constant_ranges.size());

	VkShaderStageFlags stage_mask = 0;

	auto insert_ranges = [&stage_mask, &out]
		(const std::vector<VkPushConstantRange> &ranges) -> ev2::Result
	{
		ev2::Result result = ev2::SUCCESS;
		for (const VkPushConstantRange &range : ranges) {
			if (stage_mask & range.stageFlags) {
				result = set_error(
					ev2::EBAD_SHADER, "Overlapping stage flags on push constant range");
				break;
			}
			out.push_constant_ranges.push_back(range);
		}

		return result;
	};

	result = insert_ranges(a.push_constant_ranges);

	if (result != ev2::SUCCESS)
		return result;

	result = insert_ranges(b.push_constant_ranges);

	if (result != ev2::SUCCESS)
		return result;

	std::sort(out.push_constant_ranges.begin(), out.push_constant_ranges.end(),
		[](const VkPushConstantRange &r1, const VkPushConstantRange &r2) -> bool
		{
			return r1.offset < r2.offset;
		});

	return result;
}

enum BasePipelineInitFlagBits
{
	USE_BASE_DESCRIPTOR_SETS = 0x1
};
typedef uint32_t BasePipelineInitFlags;

/// @brief Generate VkDescriptorSetLayout objects 
static ev2::Result generate_shader_descriptor_layouts(
	ev2::GfxContext *ctx,
	const ev2::ShaderLayoutMapping &shader_layout,
	std::vector<VkDescriptorSetLayout> &layouts,
	BasePipelineInitFlags flags
)
{
	uint32_t max_set = 0;
	for (const auto &[set, layout_bindings] : shader_layout.set_binding_infos) {
		max_set = std::max(set, max_set);
	}

	layouts.resize(max_set + 1, VK_NULL_HANDLE);

	// For unused slots, create an 'empty' descriptor layout
	for (uint32_t set = 0; set <= max_set; ++set) {
		std::vector<VkDescriptorBindingFlags> binding_flags;

		VkDescriptorSetLayoutBindingFlagsCreateInfo flag_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		};

		VkDescriptorSetLayoutCreateInfo create_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		};

		if (auto it = shader_layout.set_binding_infos.find(set); 
			it != shader_layout.set_binding_infos.end()) {

			if ((flags & USE_BASE_DESCRIPTOR_SETS) && set < EV2_BASE_SET_COUNT) {
				layouts[set] = ctx->get_base_descriptor_set_layout(set);
				continue;
			} else {
				const std::vector<VkDescriptorSetLayoutBinding> &bindings = it->second.bindings;
				uint32_t binding_count = (uint32_t)bindings.size();

				create_info.pBindings = bindings.data();
				create_info.bindingCount = binding_count;
				create_info.flags = 0;

				binding_flags.resize(binding_count, 0);
				
				for (uint32_t b = 0; b < binding_count; ++b) {
					const ev2::ShaderBindingInfo &info = it->second.infos[b];
					binding_flags[b] |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

					//if (info.is_variable_sized)
					//	binding_flags[b] |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
				}

				flag_info.pBindingFlags = binding_flags.data();
				flag_info.bindingCount = (uint32_t)binding_flags.size();

				create_info.pNext = &flag_info;
			}	
		} else {
			create_info.flags = 0;
		}

		VkResult result = vkCreateDescriptorSetLayout(
			ctx->device, &create_info, nullptr, &layouts[set]);

		if (result != VK_SUCCESS)
			return set_error(ev2::ELOAD_FAILED, "Failed to create descriptor set layout");
	}
	return ev2::SUCCESS;
}

static ev2::Result initialize_base_pipeline(
	ev2::GfxContext *ctx, 
	ev2::BasePipeline *base, 
	std::shared_ptr<const ev2::ShaderLayoutMapping> shader_layout,
	BasePipelineInitFlags flags,
	VkShaderStageFlags stage
)
{
	if (!shader_layout) {
		log_error("No shader layout");
		return ev2::EBAD_SHADER;
	}

	std::vector<VkDescriptorSetLayout> final_layouts;

	ev2::Result result = 
		generate_shader_descriptor_layouts(ctx, *shader_layout, final_layouts, flags);
	if (result)
		return result;

	VkPushConstantRange push_constant_range = {
		.stageFlags = stage,
		.offset = 0,
		.size = ctx->caps.limits.maxPushConstantsSize, 
	};

	VkPipelineLayoutCreateInfo layout_info = {
    	.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    	.setLayoutCount = (uint32_t)final_layouts.size(),
    	.pSetLayouts =  final_layouts.data(),
    	.pushConstantRangeCount = 1,
    	.pPushConstantRanges = &push_constant_range,
	};

	VkResult vk_result = 
		vkCreatePipelineLayout(ctx->device, &layout_info, nullptr, &base->layout); 

    if (vk_result != VK_SUCCESS) {
        result = set_error(ev2::ELOAD_FAILED, "failed to create graphics pipeline layout!");
		goto cleanup;
    }

	base->stage_mask = stage;
	base->set_layouts = std::move(final_layouts); 
	base->layout_map = shader_layout;
	return result;

cleanup:
	for (VkDescriptorSetLayout layout : final_layouts) {
		if (layout)
			vkDestroyDescriptorSetLayout(ctx->device, layout, nullptr);
	}
	return result;
}

static void destroy_base_pipeline(ev2::GfxContext *ctx, ev2::BasePipeline* base)
{
	if (base->layout)
			vkDestroyPipelineLayout(ctx->device, base->layout, nullptr);
	if (base->pipeline)
			vkDestroyPipeline(ctx->device, base->pipeline, nullptr);

}

static ev2::Result initialize_gfx_pipeline_vk_pipeline(
	ev2::GfxContext *ctx, const GfxPipelineInfo *info, ev2::GfxPipeline *pipeline)
{
	const ev2::Shader *vert = ctx->get_shader(pipeline->vert);
	const ev2::Shader *frag = ctx->get_shader(pipeline->frag);

	VertexInputLayout vertex_layout {};

	std::string sys_vert_path = ctx->assets->get_system_path(info->vert_path.c_str());
	ev2::Result result = parse_vertex_layout(sys_vert_path.c_str(), &vertex_layout);

	if (result)
		return result;

	//------------------------------------------------------------------------------
	// Populate create info

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{
    	.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    	.vertexBindingDescriptionCount = (uint32_t)vertex_layout.bindings.size(),
    	.pVertexBindingDescriptions = vertex_layout.bindings.data(),
    	.vertexAttributeDescriptionCount = (uint32_t)vertex_layout.attributes.size(),
    	.pVertexAttributeDescriptions = vertex_layout.attributes.data(),
	};

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
    	.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    	.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    	.primitiveRestartEnable = VK_FALSE,
	};

	VkPipelineShaderStageCreateInfo vertShaderStageInfo{
    	.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    	.stage = VK_SHADER_STAGE_VERTEX_BIT,
    	.module = vert->shader_module,
    	.pName = "main",
	};

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{
    	.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    	.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
    	.module = frag->shader_module,
    	.pName = "main",
	};

    VkPipelineShaderStageCreateInfo shaderStages[] = {
		vertShaderStageInfo, 
		fragShaderStageInfo
	};

    const std::vector<VkDynamicState> dynamicStates = ev2::get_dynamic_states(ctx);

    VkPipelineDynamicStateCreateInfo dynamicState{
    	.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    	.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
    	.pDynamicStates = dynamicStates.data(),
	};

    VkPipelineViewportStateCreateInfo viewportState{
    	.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    	.viewportCount = 1,
    	.scissorCount = 1,
	};

    VkPipelineRasterizationStateCreateInfo rasterizer{
    	.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    	.depthClampEnable = VK_FALSE,
    	.rasterizerDiscardEnable = VK_FALSE,
    	.polygonMode = VK_POLYGON_MODE_FILL,
    	.cullMode = VK_CULL_MODE_NONE,
    	.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    	.depthBiasEnable = VK_FALSE,
    	.depthBiasConstantFactor = 0.0f, // Optional
    	.depthBiasClamp = 0.0f, // Optional
    	.depthBiasSlopeFactor = 0.0f, // Optional
    	.lineWidth = 1.0f,
	};

    VkPipelineMultisampleStateCreateInfo multisampling{
    	.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    	.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    	.sampleShadingEnable = VK_FALSE,
    	.minSampleShading = 1.0f, // Optional
    	.pSampleMask = nullptr, // Optional
    	.alphaToCoverageEnable = VK_FALSE, // Optional
    	.alphaToOneEnable = VK_FALSE, // Optional
	};

    VkPipelineColorBlendAttachmentState colorBlendAttachment{
    	.blendEnable = VK_FALSE,
    	.srcColorBlendFactor = VK_BLEND_FACTOR_ONE, // Optional
    	.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO, // Optional
    	.colorBlendOp = VK_BLEND_OP_ADD, // Optional
    	.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, // Optional
    	.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, // Optional
    	.alphaBlendOp = VK_BLEND_OP_ADD, // Optional
    	.colorWriteMask = 
			VK_COLOR_COMPONENT_R_BIT | 
			VK_COLOR_COMPONENT_G_BIT | 
			VK_COLOR_COMPONENT_B_BIT | 
			VK_COLOR_COMPONENT_A_BIT,
	};

    VkPipelineColorBlendStateCreateInfo colorBlending{
    	.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    	.logicOpEnable = VK_FALSE,
    	.logicOp = VK_LOGIC_OP_COPY, // Optional
    	.attachmentCount = 1,
    	.pAttachments = &colorBlendAttachment,
    	.blendConstants = {
			0.f,
			0.f,
			0.f,
			0.f,
		}
	};

	VkStencilOpState stencilState{
		.failOp = VK_STENCIL_OP_KEEP,
		.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP,
		.depthFailOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP,
		.compareOp = VK_COMPARE_OP_ALWAYS,
		.compareMask = 0xFF,
		.writeMask = 0xFF,
		.reference = 0,
	};

	VkPipelineDepthStencilStateCreateInfo depthStencilState = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.flags = 0,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
		.depthBoundsTestEnable = VK_TRUE,
		.stencilTestEnable = VK_FALSE,
		.front = stencilState,
		.back = stencilState,	
		.minDepthBounds = 0.f,
		.maxDepthBounds = 1.f,
	};

	//TODO: For multi pass rendering, will need to change
	VkPipelineRenderingCreateInfo renderingInfo = get_swapchain_rendering_info(ctx);

	VkGraphicsPipelineCreateInfo pipelineInfo{
    	.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &renderingInfo,
    	.stageCount = 2,
    	.pStages = shaderStages,
    	.pVertexInputState = &vertexInputInfo,
    	.pInputAssemblyState = &inputAssembly,
    	.pViewportState = &viewportState,
    	.pRasterizationState = &rasterizer,
    	.pMultisampleState = &multisampling,
    	.pDepthStencilState = &depthStencilState,
    	.pColorBlendState = &colorBlending,
    	.pDynamicState = &dynamicState,

    	.layout = pipeline->base.layout,
    	.renderPass = VK_NULL_HANDLE,
    	.subpass = 0,

    	.basePipelineHandle = VK_NULL_HANDLE,
    	.basePipelineIndex = -1,
	};

	VkResult vk_result = vkCreateGraphicsPipelines(
		ctx->device, VK_NULL_HANDLE, 1, &pipelineInfo, 
		nullptr, &pipeline->base.pipeline);

    if (vk_result != VK_SUCCESS) {
    	return set_error(ev2::ELOAD_FAILED, "failed to create graphics pipeline!");
    }

	return ev2::SUCCESS;
}

static ev2::Result initialize_gfx_pipeline(
	ev2::GfxContext *ctx, 
	const GfxPipelineInfo *info,
	ev2::GfxPipeline *out
) 
{
	ev2::Result result = ev2::SUCCESS;

	ev2::ShaderID vert_handle = ev2::load_shader(ctx, info->vert_path.c_str());
	if (!vert_handle.is_valid()) {
		return set_error(ev2::ELOAD_FAILED, "Failed to load vertex shader");
	}

	ev2::ShaderID frag_handle = ev2::load_shader(ctx, info->frag_path.c_str());
	if (!frag_handle.is_valid()) {
		return set_error(ev2::ELOAD_FAILED, "Failed to load fragment shader");
;
	}

	ev2::GfxPipeline pipeline = {
		.vert = vert_handle,
		.frag = frag_handle,
	};

	const ev2::Shader *vert = ctx->get_shader(vert_handle);
	const ev2::Shader *frag = ctx->get_shader(frag_handle);

	std::shared_ptr<ev2::ShaderLayoutMapping> merged_layout = 
		std::make_shared<ev2::ShaderLayoutMapping>();

	result = merge_shader_layouts(*vert->layout_map, *frag->layout_map, *merged_layout);

	if (result)
		return set_error(result, "Incompatible shader layouts:\n\tvert=%s\n\tfrag=%s",
				   info->vert_path.c_str(), info->frag_path.c_str());

	result = initialize_base_pipeline(
		ctx, &pipeline.base, merged_layout, USE_BASE_DESCRIPTOR_SETS,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
	);

	if (result)
		return result;

	result = initialize_gfx_pipeline_vk_pipeline(ctx, info, &pipeline);

	if (result)
		return result;

	*out = std::move(pipeline);

	return result;
}

static std::string get_gfx_pipeline_info(ev2::GfxContext *ctx, ev2::GfxPipeline *p_pipeline)
{
	const char *vert = ctx->assets->get_entry((AssetID)p_pipeline->vert.id)->path;
	const char *frag = ctx->assets->get_entry((AssetID)p_pipeline->frag.id)->path;

	const char *fmt = 
		"\tvert: " COLORIZE_PATH(%s)"\n"
		"\tfrag: " COLORIZE_PATH(%s)"";

	std::string buf;
	buf.resize((strlen(fmt) + strlen(vert) + strlen(frag)) + 1);
	
	snprintf(buf.data(), buf.size(), fmt, vert, frag);
	return buf;
}

static void destroy_gfx_pipeline(ev2::GfxContext *ctx, ev2::GfxPipeline *pipeline)
{
	for (size_t i = 0; i < pipeline->base.set_layouts.size(); ++i) {
		if (i >= EV2_BASE_SET_COUNT)
			vkDestroyDescriptorSetLayout(ctx->device, pipeline->base.set_layouts[i], nullptr);
	}

	destroy_base_pipeline(ctx, &pipeline->base);
}

static ev2::Result gfx_pipeline_create_callback(
	ev2::GfxContext *ctx, 
	ev2::GfxPipeline **pp_pipeline, 
	const char *path
)
{
	ev2::Result result = ev2::SUCCESS;
	std::string syspath = ctx->assets->get_system_path(path);

	if (!fs::exists(syspath)) {
		log_error("File does not exist: %s", syspath.c_str());
		return ev2::ELOAD_FAILED;
	}

	std::unique_ptr<ev2::GfxPipeline> p_pipeline(new ev2::GfxPipeline{});

	GfxPipelineInfo gfx_info;
	try {
		result = parse_gfx_pipeline_file(&gfx_info,syspath.c_str());
	}catch (const YAML::BadFile& e) {
		return set_error(ev2::ELOAD_FAILED, "Error while parsing YAML: %s",e.what());
	}  catch (const YAML::Exception& e) {
		return set_error(ev2::ELOAD_FAILED, "Error while parsing YAML: %s",e.what());
	} catch (...) {
		return set_error(ev2::ELOAD_FAILED, "Error while parsing YAML: unknown");
	}

	if (result) 
		return result;

	result = initialize_gfx_pipeline(
		ctx, 
		&gfx_info,
		p_pipeline.get() 
	);

	if (result) {
		log_error("Failed to initialize shaders for %s", path);
		return result;
	}

	std::string info = get_gfx_pipeline_info(ctx, p_pipeline.get());
	log_info("Graphics pipeline : " COLORIZE_PATH(%s)"\n%s", path, info.c_str());

	*pp_pipeline = p_pipeline.release();

	return result;
}

static ev2::Result base_pipeline_post_reload(ev2::GfxContext *ctx, 
											 ev2::BasePipeline *old_pipeline,
											 ev2::BasePipeline *new_pipeline)
{
	new_pipeline->active_bindings.reserve(old_pipeline->active_bindings.size());

	for (ev2::BindingsID id : old_pipeline->active_bindings)
	{
		if (!ctx->bindings_pool->is_valid(to_pool_id(id)))
			continue;

	  	ev2::Bindings *bindings = ctx->get_bindings(id); 
		bindings->pipeline_layout = new_pipeline->layout;
		bindings->set_layout = new_pipeline->set_layouts[bindings->index];

		new_pipeline->active_bindings.push_back(id);
	}

	return ev2::SUCCESS;
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
	ev2::GfxPipeline *new_pipeline;
	ev2::Result res = gfx_pipeline_create_callback(ctx, &new_pipeline, path);

	if (res != ev2::SUCCESS)
		return res;

	ev2::GfxPipeline *pipeline = static_cast<ev2::GfxPipeline*>(*usr);

	res = base_pipeline_post_reload(ctx, &pipeline->base, &new_pipeline->base);

	if (res != ev2::SUCCESS) {
		destroy_gfx_pipeline(ctx, new_pipeline);
		return res;
	}

	destroy_gfx_pipeline(ctx, pipeline);
	*usr = new_pipeline;

	return res;
}

//------------------------------------------------------------------------------
// Compute shaders

static ev2::Result compute_pipeline_create_callback(
	ev2::GfxContext *ctx, 
	ev2::ComputePipeline **pp_pipeline, 
	const char *path
)
{
	ev2::ShaderID shader_handle = EV2_NULL_HANDLE(Shader);

	std::string path_str = path;
	bool is_from_config = path_str.ends_with(".yaml");

	if (!is_from_config) {
		path_str += ".comp.spv";
		shader_handle = ev2::load_shader(ctx, path_str.c_str());
	} else {
		log_error("Compute pipeline .yaml configs are not yet supported; must load as shader directly");
	}

	if (!EV2_VALID(shader_handle))
		return ev2::ELOAD_FAILED;
	
	ev2::Shader *shader = ctx->get_shader(shader_handle);

	std::unique_ptr<ev2::ComputePipeline> pipeline (new ev2::ComputePipeline);

	ev2::Result result = initialize_base_pipeline(
		ctx, &pipeline->base, shader->layout_map, 0,
		VK_SHADER_STAGE_COMPUTE_BIT
	);

	if (result)
		return result;

	VkComputePipelineCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.flags = 0,
		.stage = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.flags = 0,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = shader->shader_module,
			.pName = "main",
			.pSpecializationInfo = nullptr
		},
		.layout = pipeline->base.layout,
		.basePipelineHandle = VK_NULL_HANDLE,
		.basePipelineIndex = 0,
	};

	VkResult vk_result = vkCreateComputePipelines(
		ctx->device, 
		VK_NULL_HANDLE,
		1, &create_info, 
		nullptr, 
		&pipeline->base.pipeline
	);

	if (vk_result != VK_SUCCESS) {
		result = set_error(ev2::ELOAD_FAILED, "Failed to create compute pipeline");
		goto cleanup;
	}

	pipeline->shader = shader_handle;

	*pp_pipeline = pipeline.release();

	return ev2::SUCCESS;
cleanup:
	destroy_base_pipeline(ctx, &pipeline->base);
	return result;
}

static void destroy_compute_pipeline(ev2::GfxContext *ctx, ev2::ComputePipeline *pipeline)
{
	for (size_t i = 0; i < pipeline->base.set_layouts.size(); ++i) {
		vkDestroyDescriptorSetLayout(ctx->device, pipeline->base.set_layouts[i], nullptr);
	}
	destroy_base_pipeline(ctx, &pipeline->base);
	ev2::unload_shader(ctx, pipeline->shader);

	delete pipeline;
}

static void compute_pipeline_destroy_callback(ev2::GfxContext *ctx, void* usr) 
{
	ev2::ComputePipeline *pipeline = static_cast<ev2::ComputePipeline*>(usr);
	destroy_compute_pipeline(ctx, pipeline);
}

static ev2::Result compute_pipeline_reload_callback(ev2::GfxContext *ctx, void** usr, const char *path)
{
	ev2::ComputePipeline* new_pipeline = nullptr;

	ev2::Result res = compute_pipeline_create_callback(ctx, &new_pipeline, path);

	if (res != ev2::SUCCESS)
		return res;
	
	ev2::ComputePipeline *pipeline = static_cast<ev2::ComputePipeline*>(*usr);

	res = base_pipeline_post_reload(ctx, &pipeline->base, &new_pipeline->base);

	if (res != ev2::SUCCESS) {
		destroy_compute_pipeline(ctx, new_pipeline);
		return res;
	}
	
	*usr = new_pipeline;

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

	ev2::Shader *shader = nullptr; 

	ev2::Result res = shader_create_callback(ctx, &shader, path);

	if (res == ev2::SUCCESS) {
		id = ctx->assets->allocate(&vtbl, shader, path); 
		return ShaderID{id};
	}

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

	ev2::GfxPipeline *pipeline = nullptr;
	ev2::Result res = gfx_pipeline_create_callback(ctx, &pipeline, path);

	if (res == ev2::SUCCESS) {
		id = ctx->assets->allocate(&vtbl, pipeline, path);

		if (ctx->assets->reloader) {
			ctx->assets->reloader->add_dependency((AssetID)pipeline->vert.id, id);
			ctx->assets->reloader->add_dependency((AssetID)pipeline->frag.id, id);
		}

		return GfxPipelineID{id};
	}

	return EV2_NULL_HANDLE(GfxPipeline);
}

void unload_graphics_pipeline(GfxContext *ctx, GfxPipelineID pipe)
{
}

//------------------------------------------------------------------------------

ComputePipelineID load_compute_pipeline(GfxContext *ctx, const char *path)
{
	std::string_view path_str (path);
	if (path_str.ends_with(".comp.spv") || path_str.ends_with(".comp")) {
		log_warn("Compute pipeline loaded via shader file path: internal name will be different");
	}

	AssetID id = ctx->assets->load(path);

	if (id)
		return ComputePipelineID{id};

	static AssetVTable vtbl = {
		.reload = compute_pipeline_reload_callback,
		.destroy = compute_pipeline_destroy_callback,
	};

	ev2::ComputePipeline *pipeline = nullptr;
	ev2::Result res = compute_pipeline_create_callback(ctx, &pipeline, path);

	if (res == ev2::SUCCESS) {
		id = ctx->assets->allocate(&vtbl, pipeline, path);

		if (ctx->assets->reloader) {
			ctx->assets->reloader->add_dependency((AssetID)pipeline->shader.id, id);
		}

		return ComputePipelineID{id};
	}

	return EV2_NULL_HANDLE(ComputePipeline);
}

void unload_compute_pipeline(GfxContext *ctx, ComputePipelineID pipe)
{
}

//------------------------------------------------------------------------------

static BindingsID create_bindings_internal(
	GfxContext *ctx, BasePipeline &pipeline, 
	uint32_t index, BindingMode mode, VkPipelineBindPoint bind_point)
{
	VkDescriptorSetLayout set_layout = pipeline.set_layouts[index]; 
	VkDescriptorSet set = VK_NULL_HANDLE;

	VkDescriptorSetVariableDescriptorCountAllocateInfo variable_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
	};

	if (mode == BINDING_MODE_STATIC) {
		VkDescriptorSetAllocateInfo alloc_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = ctx->static_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &set_layout,
		};

		VkResult vk_result = 
			vkAllocateDescriptorSets(ctx->device, &alloc_info, &set); 

		if (vk_result != VK_SUCCESS) {
			log_error("Failed to allocate descriptor sets from static pool");
			return EV2_NULL_HANDLE(Bindings);
		}
	}

	BindingsID id = ctx->emplace_bindings(Bindings{
		.layout_map = pipeline.layout_map,
		.pipeline_layout = pipeline.layout,
		.bind_point = bind_point,
		.set_layout = set_layout,
		.descriptor_set = set,
		.index = index,
		.mode = mode,
	});
	pipeline.active_bindings.push_back(id);
	return id;
}

//------------------------------------------------------------------------------
// Interface
//------------------------------------------------------------------------------

BindingsID create_bindings(GfxContext *ctx, GfxPipelineID pipeline_id, 
								 uint32_t index, BindingMode mode)
{
	GfxPipeline *pipeline = ctx->get_gfx_pipeline(pipeline_id);

	if (!pipeline)
		return EV2_NULL_HANDLE(Bindings);

	if (index < EV2_BASE_SET_COUNT) {
		log_error("Descriptor set index %d is reserved by the engine", index);
		return EV2_NULL_HANDLE(Bindings);
	}

	if (!pipeline->base.layout_map->set_binding_infos.contains(index)) {
		log_error("Descriptor set index %d does not exist for graphics pipeline '%s'", 
			index, ctx->get_gfx_pipeline_name(pipeline_id));
		return EV2_NULL_HANDLE(Bindings);
	}

	return create_bindings_internal(
		ctx, pipeline->base, index, mode, VK_PIPELINE_BIND_POINT_GRAPHICS);
}

BindingsID create_bindings(GfxContext *ctx, ComputePipelineID pipeline_id, 
								 uint32_t index, BindingMode mode)
{
	ComputePipeline *pipeline = ctx->get_compute_pipeline(pipeline_id);

	if (!pipeline)
		return EV2_NULL_HANDLE(Bindings);

	if (!pipeline->base.layout_map->set_binding_infos.contains(index)) {
		log_error("Descriptor set index %d does not exist for compute pipeline '%s'", 
			index, ctx->get_compute_pipeline_name(pipeline_id));
		return EV2_NULL_HANDLE(Bindings);
	}

	return create_bindings_internal(
		ctx, pipeline->base, index, mode, VK_PIPELINE_BIND_POINT_COMPUTE);
}

void destroy_bindings(GfxContext *ctx, BindingsID bindings_id)
{
	Bindings *bindings = ctx->get_bindings(bindings_id); 

	ctx->wait_for_frame_completion(ctx->frame_counter ? ctx->frame_counter - 1 : 0);

	if (bindings->mode == BINDING_MODE_STATIC) {
		VkResult vk_result = vkFreeDescriptorSets(
			ctx->device, ctx->static_descriptor_pool, 1, &bindings->descriptor_set);

		if (vk_result != VK_SUCCESS)
			log_error("Failed to reset descriptor set");
	}

	ctx->bindings_pool->deallocate(to_pool_id(bindings_id));
}

Result reset_bindings(GfxContext *ctx, BindingsID bindings_handle)
{
	Bindings *bindings = ctx->get_bindings(bindings_handle);
	VkResult vk_result = VK_SUCCESS;

	VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

	switch (bindings->mode) {
		case BINDING_MODE_STATIC:
			vk_result = vkFreeDescriptorSets(
				ctx->device, ctx->static_descriptor_pool, 1, &bindings->descriptor_set);

			if (vk_result != VK_SUCCESS)
				return ev2::EINVALID_BINDING;

			descriptor_pool = ctx->static_descriptor_pool;
			break;
		case BINDING_MODE_DYNAMIC:
			descriptor_pool = ctx->get_current_frame()->descriptor_pool;
	}

	VkDescriptorSetAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &bindings->set_layout,
	};

	 vk_result = vkAllocateDescriptorSets(
		ctx->device, &alloc_info, &bindings->descriptor_set); 

	if (vk_result != VK_SUCCESS)
		return set_error(ev2::EINVALID_BINDING, 
			"vkAllocateDescriptorSets failed!");

	return ev2::SUCCESS;
}

ev2::Result bind_buffer(
	GfxContext *ctx, 
	BindingsID id, 
	const char *name,
	BufferID buffer_handle, 
	size_t offset, 
	size_t size
) 
{
	Bindings *bindings = ctx->get_bindings(id);
	const Buffer *buffer = ctx->get_buffer(buffer_handle);

	uint32_t set;
	VkDescriptorSetLayoutBinding binding; 

	if (!bindings->descriptor_set) {
		return set_error(EINVALID_BINDING,
				   "descriptor set is VK_NULL_HANDLE, did you remember to call 'flush_bindings'?");
	}

	if (!bindings->layout_map->find(name, &set, &binding)) {
		return set_error(ev2::EINVALID_BINDING, "binding does not exist : %s", name);
	}

	if (set != bindings->index) {
		return set_error(ev2::EINVALID_BINDING,
			"mismatched binding: "
			"%s (set=%d, binding=%d) does not belong to set %d", 
			name, set, binding.binding, bindings->index
		);
	}

	VkDescriptorBufferInfo buffer_info = {
		.buffer = buffer->buffer,
		.offset = offset,
		.range = size,
	};

	VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = bindings->descriptor_set,
		.dstBinding = binding.binding,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = binding.descriptorType,
	};

	bindings->writes.push_back(write);
	bindings->info.push_back({
		.buffer = buffer_info,
		.type = Bindings::Buffer
	});
	return ev2::SUCCESS;
}

ev2::Result bind_texture(
	GfxContext *ctx, 
	BindingsID id,
	const char *name,
	TextureID texture_handle  
) 
{
	Bindings *bindings = ctx->get_bindings(id);
	const Texture *texture = ctx->get_texture(texture_handle);

	uint32_t set;
	VkDescriptorSetLayoutBinding binding; 

	if (!bindings->descriptor_set) {
		return set_error(EINVALID_BINDING,
				   "descriptor set is VK_NULL_HANDLE, did you remember to call 'flush_bindings'?");
	}
	if (!bindings->layout_map->find(name, &set, &binding)) {
		return set_error(ev2::EINVALID_BINDING, "binding does not exist : %s", name);
	}

	if (set != bindings->index) {
		set_error(ev2::EINVALID_BINDING,
			"mismatched binding: "
			"%s (set=%d, binding=%d) does not belong to set %d", 
			name, set, binding.binding, bindings->index
		);
	}

	VkDescriptorImageInfo image_info = {
		.sampler = texture->sampler,
		.imageView = texture->view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = bindings->descriptor_set,
		.dstBinding = binding.binding,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = binding.descriptorType,
	};

	bindings->writes.push_back(write);
	bindings->info.push_back({
		.image = image_info,
		.type = Bindings::Image
	});
	return ev2::SUCCESS;
}

static ev2::Result bind_image_internal(
	GfxContext *ctx,
	BindingsID id,
	const char *name, 
	uint32_t dst_index,
	ImageID image_handle,
	uint32_t level,
	uint32_t layer
)
{
	Bindings *bindings = ctx->get_bindings(id);
	Image *image = ctx->get_image(image_handle);

	uint32_t set;
	VkDescriptorSetLayoutBinding binding; 

	if (!bindings->descriptor_set) {
		return set_error(EINVALID_BINDING,
				   "descriptor set is VK_NULL_HANDLE, did you remember to call 'flush_bindings'?");
	}
	if (!bindings->layout_map->find(name, &set, &binding)) {
		return set_error(ev2::EINVALID_BINDING, "binding does not exist : %s", name);
	}

	if (set != bindings->index) {
		return set_error(ev2::EINVALID_BINDING,
			"mismatched binding: "
			"%s (set=%d, binding=%d) does not belong to set %d", 
			name, set, binding.binding, bindings->index
		);
	}

	if (image->format == VK_FORMAT_UNDEFINED) {
		return set_error(ev2::EINVALID_BINDING, "image %d has format VK_FORMAT_UNDEFINED",
				   image_handle.id);
	}
	
	ImageViewKey view_key = {
		.type = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = image->aspect_mask,
		.baseMipLevel = level,
		.levelCount = 1,
		.baseArrayLayer = layer,
		.layerCount = 1,
		.format = image->format
	};

	VkImageView view = get_image_view(ctx, image, view_key);

	VkDescriptorImageInfo image_info = {
		.sampler = nullptr,
		.imageView = view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = bindings->descriptor_set,
		.dstBinding = binding.binding,
		.dstArrayElement = dst_index,
		.descriptorCount = 1,
		.descriptorType = binding.descriptorType,
	};

	bindings->writes.push_back(write);
	bindings->info.push_back({
		.image = image_info,
		.type = Bindings::Image
	});

	return ev2::SUCCESS;
}
	
ev2::Result bind_image_indexed(
	GfxContext *ctx,
	BindingsID id,
	const char *name,
	uint32_t dst_index,
	ImageID image_handle,
	uint32_t level,
	uint32_t layer
)
{
	return bind_image_internal(ctx, id, name, dst_index, image_handle, level, layer);
}

ev2::Result bind_image(
	GfxContext *ctx,
	BindingsID id,
	const char *name, 
	ImageID image_handle,
	uint32_t level,
	uint32_t layer
)
{
	return bind_image_internal(ctx, id, name, 0, image_handle, level, layer);
}

void flush_bindings(GfxContext *ctx, BindingsID id)
{
	Bindings *bindings = ctx->get_bindings(id);

	for (size_t i = 0; i < bindings->writes.size(); ++i) {
		const Bindings::Info &info = bindings->info[i];

		switch(info.type) {
			case Bindings::Buffer:
				bindings->writes[i].pBufferInfo = &info.buffer;
				break;
			case Bindings::Image:
				bindings->writes[i].pImageInfo = &info.image;
				break;
		}
	}

	if (bindings->writes.size()) {
		vkUpdateDescriptorSets(
			ctx->device, 
			(uint32_t)bindings->writes.size(),
			bindings->writes.data(),
			0, nullptr);

		bindings->writes.clear();
		bindings->info.clear();
	}
}

//------------------------------------------------------------------------------
// Internal interface

VkPipelineRenderingCreateInfoKHR get_swapchain_rendering_info(ev2::GfxContext *ctx)
{
	return VkPipelineRenderingCreateInfo{
		.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount    = 1,
		.pColorAttachmentFormats = &ctx->swap_chain.image_format,
		.depthAttachmentFormat   = VK_FORMAT_D32_SFLOAT_S8_UINT,
		.stencilAttachmentFormat = VK_FORMAT_D32_SFLOAT_S8_UINT,
	};
}

std::vector<VkDynamicState> get_dynamic_states(GfxContext *ctx)
{
	return {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,

	};
}

VkDescriptorSetLayout generate_per_frame_descriptor_set_layout(ev2::GfxContext *ctx)
{
	VkDescriptorSetLayoutBinding bindings[] = {
		// Frame global ubo
		VkDescriptorSetLayoutBinding{
			.binding = EV2_FRAME_UBO_BINDING,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_ALL,
		}
	};

	VkDescriptorSetLayoutCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = 0,
		.bindingCount = sizeof(bindings)/sizeof(VkDescriptorSetLayoutBinding),
		.pBindings = bindings,
	};

	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	VkResult result = vkCreateDescriptorSetLayout(ctx->device, &create_info, nullptr, &layout);

	if (result != VK_SUCCESS)
		return VK_NULL_HANDLE;
	return layout;
}
VkDescriptorSetLayout generate_per_pass_descriptor_set_layout(ev2::GfxContext *ctx)
{
	VkDescriptorSetLayoutBinding bindings[] = {
		VkDescriptorSetLayoutBinding{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_ALL,
		}
	};

	VkDescriptorSetLayoutCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = 0,
		.bindingCount = sizeof(bindings)/sizeof(VkDescriptorSetLayoutBinding),
		.pBindings = bindings,
	};

	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	VkResult result = vkCreateDescriptorSetLayout(ctx->device, &create_info, nullptr, &layout);

	if (result != VK_SUCCESS)
		return VK_NULL_HANDLE;
	return layout;
}
VkDescriptorSetLayout generate_bindless_descriptor_set_layout(ev2::GfxContext *ctx)
{
	VkDescriptorSetLayoutBinding bindings[] = {
	};

	VkDescriptorSetLayoutCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = 0,
		.bindingCount = sizeof(bindings)/sizeof(VkDescriptorSetLayoutBinding),
		.pBindings = bindings,
	};

	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	VkResult result = vkCreateDescriptorSetLayout(ctx->device, &create_info, nullptr, &layout);

	if (result != VK_SUCCESS)
		return VK_NULL_HANDLE;
	return layout;
}

}; // namespace ev2

