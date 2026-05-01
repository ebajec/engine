#ifndef EV2_PIPELINE_IMPL_H
#define EV2_PIPELINE_IMPL_H

#include "backends/vulkan/def_vulkan.h"

#include "ev2/resource.h"
#include "ev2/pipeline.h"

#include <unordered_map>
#include <string>
#include <vector>
#include <memory>

#include <cstdint>

template<typename T>
class static_flat_map {
	size_t count;
	void *data;
};

namespace ev2 {

enum ReservedDescriptorSet {
	EV2_RESERVED_DESCRIPTOR_SET_PER_FRAME = 0,
	EV2_RESERVED_DESCRIPTOR_SET_PER_PASS = 1,
	EV2_RESERVED_DESCRIPTOR_SET_BINDLESS = 2,
	EV2_RESERVED_DESCRIPTOR_SET_MAX
};

struct ShaderLayout
{
	struct BindingEntry {
		uint32_t set;
		uint32_t idx;
	};
	std::unordered_map<std::string, BindingEntry> binding_names;
	std::unordered_map<uint32_t, 
		std::vector<VkDescriptorSetLayoutBinding>
	> bindings;
};

struct Shader
{
	VkShaderModule 	shader_module;
	ShaderStage stage;
	ShaderLayout layout;
};

struct GfxPipeline
{
	ev2::ShaderID vert;
	ev2::ShaderID frag;

	std::unordered_map<uint32_t, VkDescriptorSetLayout> layouts;

	VkPipelineLayout layout;
	VkPipeline pipeline;
};

struct ComputePipeline
{
	Shader shader;
	VkPipeline pipeline;
};

extern VkDescriptorSetLayout generate_per_frame_descriptor_set_layout(ev2::GfxContext *ctx);
extern VkDescriptorSetLayout generate_per_pass_descriptor_set_layout(ev2::GfxContext *ctx);
extern VkDescriptorSetLayout generate_bindless_descriptor_set_layout(ev2::GfxContext *ctx);

};

#endif // EV2_PIPELINE_IMPL_H
