#ifndef EV2_PIPELINE_IMPL_H
#define EV2_PIPELINE_IMPL_H

#include "backends/vulkan/def_vulkan.h"

#include "ev2/resource.h"
#include "ev2/pipeline.h"

#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <unordered_map>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

enum {
	EV2_BASE_SET_BINDLESS, 
	EV2_BASE_SET_PER_FRAME,
	EV2_BASE_SET_PER_PASS,
	EV2_BASE_SET_COUNT
};

#define EV2_FRAME_UBO_BINDING 0
#define EV2_PER_PASS_UBO_BINDING 0

namespace ev2 {

struct ShaderLayoutIndex
{
	struct BindingEntry {
		uint32_t set;
		uint32_t idx;
	};
	std::unordered_map<std::string, BindingEntry> binding_names;
	std::unordered_map<uint32_t, 
		std::vector<VkDescriptorSetLayoutBinding>
	> bindings;

	bool find(const char *name, 
			 uint32_t * out_set,
			 VkDescriptorSetLayoutBinding* out_binding) const {
		auto it = binding_names.find(name);
		if (it == binding_names.end())
			return false;

		*out_set = it->second.set;
		*out_binding = bindings.at(it->second.set)[it->second.idx]; 
		return true;
	}
};

struct Shader
{
	VkShaderModule 	shader_module;
	ShaderStage stage;
	std::shared_ptr<ShaderLayoutIndex> layout_index;
};

struct BasePipeline
{
	std::shared_ptr<const ShaderLayoutIndex> layout_index;
	std::vector<VkDescriptorSetLayout> set_layouts;
	VkPipelineLayout layout;
	VkPipeline pipeline;
};

struct GfxPipeline
{
	BasePipeline base;

	ev2::ShaderID vert;
	ev2::ShaderID frag;
};

struct ComputePipeline
{
	BasePipeline base;

	ev2::ShaderID shader;
};

struct ShaderBindings
{
	enum BindType {
		Buffer,
		Image
	};

	struct Info {
		union {
			VkDescriptorBufferInfo buffer;
			VkDescriptorImageInfo image;
		};
		BindType type;
	};

	std::shared_ptr<const ShaderLayoutIndex> layout_index;

	std::vector<Info> info;
	std::vector<VkWriteDescriptorSet> writes;
	std::vector<BindType> types;

	VkPipelineLayout pipeline_layout;
	VkPipelineBindPoint bind_point;

	VkDescriptorSet descriptor_set;
	uint32_t index;
};

struct Recorder {
	GfxContext *ctx;
	VkCommandBuffer command_buffer;
	uint32_t queue_family;
};

struct ViewData
{
	glm::mat4 p;
	glm::mat4 v;
	glm::mat4 pv;
	glm::vec3 center;
};

struct RenderTarget
{
	uint32_t w;
	uint32_t h;

	ImageID depth_img;
	ImageID color_img;

	VkImageView depth_view;
	VkImageView color_view;

	uint32_t flags;
};

struct Pass;

//------------------------------------------------------------------------------
// Pipeline commands

struct CmdBindComputePipeline{
	ComputePipelineID pipeline;
};
struct CmdBindGfxPipeline{
	GfxPipelineID pipeline;
};
struct CmdBindResources{
	ShaderBindingsID bindings;
};
struct CmdBindIndexBuffer{
	BufferID buffer;
	size_t offset;
};
struct CmdBindVertexBuffer{
	BufferID buffer;
	size_t offset;
};
struct CmdDispatch{
	uint32_t counts[3];
};
struct CmdUseBuffer{
	BufferID buffer;
	Usage usage;
};
struct CmdUseImage{
	ImageID image;
	Usage usage;
};
struct CmdCustom{
	uint32_t callback_id;
};

enum CmdType
{
	BindComputePipeline,
	BindGfxPipeline,
	BindResources,
	BindIndexBuffer,
	BindVertexBuffer,
	Dispatch,
	UseBuffer,
	UseImage,
	Custom
};

struct Command {
	union {
		CmdBindComputePipeline 	bind_compute_pipeline;
		CmdBindGfxPipeline 		bind_gfx_pipeline;
		CmdBindResources 		bind_resources;
		CmdBindIndexBuffer		bind_index_buffer;
		CmdBindVertexBuffer		bind_vertex_buffer;
		CmdDispatch 			dispatch;
		CmdUseBuffer 			use_buffer; 
		CmdUseImage 			use_image;
		CmdCustom				custom;
	};
	CmdType type;
};

struct RenderPass
{
	RenderTargetID target;
	ViewID view;
	Rect viewport;
	Rect scissor;

	// Contains UBO with view data.
	VkDescriptorSet descriptor_set;

	bool render_to_swapchain() {
		return !target.is_valid();
	}
};

struct Pass
{
	GfxContext *ctx;

	std::vector<std::function<void(VkCommandBuffer)>> custom_callbacks;
	std::vector<Command> cmds;
	uint32_t queue_family;

	RenderPass* gfx;
};

static inline ViewData view_data_from_matrices(float view[], float proj[])
{
	glm::mat4 view_mat = view ? glm::make_mat4(view) : glm::mat4(1.f);
	glm::mat4 proj_mat = proj ? glm::make_mat4(proj) : glm::mat4(1.f);

	return ViewData{
		.p = proj_mat,
		.v = view_mat,
		.pv = proj_mat * view_mat,
		.center = glm::vec3(0),
	};
}

extern RenderTarget *create_render_target_internal(
	GfxContext *ctx,
	uint32_t w, uint32_t h,
	VkImageView color_view,
	VkImageView depth_view,
	RenderTargetFlags flags
);

extern void destroy_render_target_internal(
	GfxContext *ctx, 
	RenderTarget* target
);

extern VkDescriptorSetLayout generate_per_frame_descriptor_set_layout(ev2::GfxContext *ctx);
extern VkDescriptorSetLayout generate_per_pass_descriptor_set_layout(ev2::GfxContext *ctx);
extern VkDescriptorSetLayout generate_bindless_descriptor_set_layout(ev2::GfxContext *ctx);

};

#endif // EV2_PIPELINE_IMPL_H
