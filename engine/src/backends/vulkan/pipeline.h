#ifndef EV2_PIPELINE_IMPL_H
#define EV2_PIPELINE_IMPL_H

#include "backends/vulkan/def_vulkan.h"

#include "ev2/resource.h"
#include "ev2/pipeline.h"
#include "robin_hood.h"

#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

#define EV2_FRAME_UBO_BINDING 0
#define EV2_PER_PASS_UBO_BINDING 0

namespace ev2 {

struct ShaderBindingInfo 
{
	bool is_variable_sized : 1 = false;
};

struct SetBindingInfo {
	std::vector<VkDescriptorSetLayoutBinding> bindings;
	std::vector<ShaderBindingInfo> infos;
};

struct ShaderLayoutMapping
{
	struct BindingEntry {
		uint32_t set;
		uint32_t idx;
	};

	robin_hood::unordered_flat_map<std::string, BindingEntry> binding_names;
	robin_hood::unordered_flat_map<uint32_t, SetBindingInfo> set_binding_infos;

	bool find(const char *name, 
			 uint32_t * out_set,
			 VkDescriptorSetLayoutBinding* out_binding) const {
		auto it = binding_names.find(name);
		if (it == binding_names.end())
			return false;

		*out_set = it->second.set;
		*out_binding = set_binding_infos.at(it->second.set).bindings[it->second.idx]; 
		return true;
	}
	std::vector<VkPushConstantRange> push_constant_ranges;
};

struct Shader
{
	VkShaderModule 	shader_module;
	VkShaderStageFlags stage_flags;
	std::shared_ptr<ShaderLayoutMapping> layout_map;
};

struct BasePipeline
{
	std::shared_ptr<const ShaderLayoutMapping> layout_map;
	std::vector<VkDescriptorSetLayout> set_layouts;
	VkPipelineLayout layout;
	VkPipeline pipeline;
	VkShaderStageFlags stage_mask;

	// Used to allow reloading at runtime.
	std::vector<BindingsID> active_bindings;
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

struct Bindings
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

	std::shared_ptr<const ShaderLayoutMapping> layout_map;

	std::vector<Info> info;
	std::vector<VkWriteDescriptorSet> writes;

	VkPipelineLayout pipeline_layout;
	VkPipelineBindPoint bind_point;

	VkDescriptorSetLayout set_layout;
	VkDescriptorSet descriptor_set;
	uint32_t index;
	BindingMode mode;
};

// CPU side only: the camera matrices a ViewID names. Uploaded to the GPU as
// part of PassData, once per pass that uses it.
struct View
{
	glm::mat4 p;
	glm::mat4 v;
	glm::mat4 pv;
	glm::vec3 center;
	glm::ivec2 resolution;
};

// Exposed to shaders in a graphics pass: the view it was recorded with,
// plus the properties of the surface it is drawing into. One entry per pass per
// frame, living in FrameContext::gpu_pass_info.
//
// Layout must match struct passdata_t in resource/shader/frame.glsl (std140).
struct PassData
{
	glm::mat4 p;
	glm::mat4 v;
	glm::mat4 pv;

	glm::vec3 center;
	float _pad0 = 0.f;

	glm::vec2 resolution;
	glm::vec2 inv_resolution;

	float near_plane;
	float far_plane;

	float _pad1[2] = {0.f, 0.f};
};
static_assert(sizeof(PassData) == 240, "PassData must match its std140 layout");

struct RenderTarget
{
	uint32_t w;
	uint32_t h;

	ImageID depth_img;
	ImageID color_img;

	VkImageView depth_view;
	VkImageView color_view;

	RenderTargetFlags flags;
};

//------------------------------------------------------------------------------
// Pipeline commands

struct CmdBindComputePipeline{
	ComputePipelineID pipeline;
};
struct CmdBindGfxPipeline{
	GfxPipelineID pipeline;
};
struct CmdBindResources{
	BindingsID bindings;
};
struct CmdBindIndexBuffer{
	BufferID buffer;
	VkDeviceSize offset;
};
struct CmdBindVertexBuffer{
	BufferID buffer;
	VkDeviceSize offset;
};
struct CmdDrawIndirect{
	BufferID buffer;
	VkDeviceSize offset;
	uint32_t count;
	uint32_t stride;
};

struct CmdPushConstant{
	BasePipeline *pipeline;

 	// offset into the push constant data buffer of the associated pass
	uint32_t src_offset;

	uint32_t offset;
	uint32_t size;
};
struct CmdClear{
	ImageID image;
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
	DrawIndirect,
	PushConstant,
	Clear,
	Dispatch,
	UseBuffer,
	UseImage,
	Custom
};

#define COMMAND_UNION_CONVERSION(Name, NameLower)\
Command(Cmd##Name in_cmd) {\
	NameLower = in_cmd;\
	type = Name;\
}

struct Command {
	union {
		CmdBindComputePipeline 	bind_compute_pipeline;
		CmdBindGfxPipeline 		bind_gfx_pipeline;
		CmdBindResources 		bind_resources;
		CmdBindIndexBuffer		bind_index_buffer;
		CmdBindVertexBuffer		bind_vertex_buffer;
		CmdDrawIndirect			draw_indirect;
		CmdPushConstant			push_constant;
		CmdClear				clear;
		CmdDispatch 			dispatch;
		CmdUseBuffer 			use_buffer; 
		CmdUseImage 			use_image;
		CmdCustom				custom;
	};
	CmdType type;

	COMMAND_UNION_CONVERSION(BindComputePipeline, bind_compute_pipeline)
	COMMAND_UNION_CONVERSION(BindGfxPipeline, bind_gfx_pipeline)
	COMMAND_UNION_CONVERSION(BindResources, bind_resources)
	COMMAND_UNION_CONVERSION(BindIndexBuffer, bind_index_buffer)
	COMMAND_UNION_CONVERSION(BindVertexBuffer, bind_vertex_buffer)
	COMMAND_UNION_CONVERSION(DrawIndirect, draw_indirect)
	COMMAND_UNION_CONVERSION(PushConstant, push_constant)
	COMMAND_UNION_CONVERSION(Clear, clear)
	COMMAND_UNION_CONVERSION(Dispatch, dispatch)
	COMMAND_UNION_CONVERSION(UseBuffer, use_buffer) 
	COMMAND_UNION_CONVERSION(UseImage, use_image)
	COMMAND_UNION_CONVERSION(Custom, custom)
};

static inline View view_data_from_matrices(float view[], float proj[])
{
	glm::mat4 view_mat = view ? glm::make_mat4(view) : glm::mat4(1.f);
	glm::mat4 proj_mat = proj ? glm::make_mat4(proj) : glm::mat4(1.f);

	return View{
		.p = proj_mat,
		.v = view_mat,
		.pv = proj_mat * view_mat,
		.center = glm::vec3(-view_mat[3]) * glm::mat3(view_mat),
	};
}

// @brief Recover the clip planes from a projection matrix, so shaders that
// linearize depth don't have to be told them separately.
//
// glm is column major, so p[col][row]. Writing z_c and w_c for the clip space
// z and w produced by p:
//
//   perspective (camera_proj_3d): w_c = -z_v, z_c = a*z_v + b, depth 0 at the
//     near plane and 1 at the far plane, which gives near = b/a and
//     far = a*near/(a + 1).
//   orthographic (camera_proj_2d): w_c = 1, z_c = a*z_v + b, so the planes are
//     just where z_c hits 0 and 1.
static inline void extract_clip_planes(const glm::mat4 &p, float *p_near, float *p_far)
{
	constexpr float eps = 1e-9f;

	const float a = p[2][2];
	const float b = p[3][2];
	const float w = p[2][3];

	float near_plane = 0.f;
	float far_plane = 1.f;

	if (fabsf(w) > eps && fabsf(a) > eps) {
		near_plane = b / a;

		const float denom = a + 1.f;
		far_plane = fabsf(denom) > eps ? (a * near_plane) / denom : near_plane;
	} else if (fabsf(a) > eps) {
		near_plane = -b / a;
		far_plane = (1.f - b) / a;
	}

	// camera_proj_2d has no meaningful clip range (its w row is 1 - z, so the
	// formulas above collapse to 0/0). Hand shaders a usable range instead of a
	// degenerate one so nothing divides by far - near and gets an infinity.
	if (!(far_plane > near_plane)) {
		near_plane = 0.f;
		far_plane = 1.f;
	}

	if (p_near) *p_near = near_plane;
	if (p_far) *p_far = far_plane;
}

static inline PassData pass_data_from_view(const View &view, glm::uvec2 resolution)
{
	glm::vec2 res = glm::vec2(resolution);

	PassData data = {
		.p = view.p,
		.v = view.v,
		.pv = view.pv,
		.center = view.center,
		.resolution = res,
		.inv_resolution = glm::vec2(
			res.x > 0.f ? 1.f/res.x : 0.f,
			res.y > 0.f ? 1.f/res.y : 0.f
		),
	};

	extract_clip_planes(view.p, &data.near_plane, &data.far_plane);

	return data;
}

extern RenderTarget *create_render_target_internal(
	GfxContext *ctx,
	uint32_t w, uint32_t h,
	VkImageView color_view,
	VkImageView depth_view,
	RenderTargetFlags flags = 0
);

extern VkResult create_depth_stencil_image(GfxContext *ctx, uint32_t w, uint32_t h,
									  ImageID* out_image, VkImageView *out_view);

extern void destroy_render_target_internal(
	GfxContext *ctx, 
	RenderTarget* target
);

extern VkDescriptorSetLayout generate_per_frame_descriptor_set_layout(ev2::GfxContext *ctx);
extern VkDescriptorSetLayout generate_per_pass_descriptor_set_layout(ev2::GfxContext *ctx);
extern VkDescriptorSetLayout generate_bindless_descriptor_set_layout(ev2::GfxContext *ctx);

};

#endif // EV2_PIPELINE_IMPL_H
