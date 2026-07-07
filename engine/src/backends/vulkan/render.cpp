#include <ev2/defines.h>

#include "backends/vulkan/context_impl.h"
#include "backends/vulkan/pipeline_impl.h"

#include <glm/gtc/type_ptr.hpp>

#include <optional>
#include <cstring>

#define DEFAULT_DEVICE_INDEX 0

constexpr uint32_t INDEX_NONE = (uint32_t)-1;

namespace ev2 {

static Result reset_frame_context(GfxContext *ctx, FrameContext *frame)
{
	VkResult vk_result = VK_SUCCESS;
	//------------------------------------------------------------------------------
	// cleaup previous state

	frame->passes.clear();
	frame->render_graph.reset();

	for (TransientCommands &commands : frame->commands) {
		vk_result = commands.reset();

		if (vk_result != VK_SUCCESS)
			return EUNKNOWN;
	}

	vk_result = vkResetDescriptorPool(ctx->device, frame->descriptor_pool, 0);

	if (vk_result != VK_SUCCESS)
		return EUNKNOWN;

	return SUCCESS;
}

VkResult create_depth_stencil_image(GfxContext *ctx, uint32_t w, uint32_t h,
									  ImageID* out_image, VkImageView *out_view)
{
	VkImageCreateInfo create_info  = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    	.flags = 0,
    	.imageType = VK_IMAGE_TYPE_2D, 
    	.format = VK_FORMAT_D32_SFLOAT_S8_UINT,
    	.extent = VkExtent3D{
			.width = w,
			.height = h,
			.depth = 1,
		},
    	.mipLevels = 1, 
    	.arrayLayers = 1,
    	.samples = VK_SAMPLE_COUNT_1_BIT,
    	.tiling = VK_IMAGE_TILING_OPTIMAL,
    	.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    	.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    	.queueFamilyIndexCount = 0,
    	.pQueueFamilyIndices = nullptr,
    	.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VmaAllocationCreateInfo alloc_info = {
		.flags = 0,
		.usage = VMA_MEMORY_USAGE_AUTO,
		.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	};

	VkImage vk_image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;

	VkResult result = 
		vmaCreateImage(ctx->allocator, &create_info, &alloc_info, &vk_image, &allocation, nullptr);

	if (result != VK_SUCCESS) {
		log_error("Failed to create depth + stencil image");
		return result;
	}

	VkImageAspectFlags aspect_mask = 
		VK_IMAGE_ASPECT_DEPTH_BIT |
		VK_IMAGE_ASPECT_STENCIL_BIT;

	VkFormat format = VK_FORMAT_D32_SFLOAT_S8_UINT;

	VkImageViewCreateInfo view_info {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.flags = 0, 
		.image = vk_image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = format,
		.components = {},
		.subresourceRange = {
			.aspectMask = aspect_mask,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		}
	};

	VkImageView view = VK_NULL_HANDLE;

	result = vkCreateImageView(ctx->device, &view_info, nullptr, &view);

	if (result != VK_SUCCESS) {
		log_error("Failed to create depth + stencil image view");
		return result;
	}

	Image image = {
		.image = vk_image,
		.allocation = allocation,
		.aspect_mask = aspect_mask,
		.format = format,
		.w = w,
		.h = h,
		.d = 1,
		.levels = 1,
	};

	*out_image = ctx->emplace_image(std::move(image));
	*out_view = view;

	return result;
}

static VkResult create_color_image(GfxContext *ctx, uint32_t w, uint32_t h,
									  ImageID* out_image, VkImageView *out_view)
{
	VkImageCreateInfo create_info  = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    	.flags = 0,
    	.imageType = VK_IMAGE_TYPE_2D, 
    	.format = ctx->swap_chain.image_format,
    	.extent = VkExtent3D{
			.width = w,
			.height = h,
			.depth = 1,
		},
    	.mipLevels = 1, 
    	.arrayLayers = 1,
    	.samples = VK_SAMPLE_COUNT_1_BIT,
    	.tiling = VK_IMAGE_TILING_OPTIMAL,
    	.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    	.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    	.queueFamilyIndexCount = 0,
    	.pQueueFamilyIndices = nullptr,
    	.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VmaAllocationCreateInfo alloc_info = {
		.flags = 0,
		.usage = VMA_MEMORY_USAGE_AUTO,
		.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	};

	VkImage vk_image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;

	VkResult result = 
		vmaCreateImage(ctx->allocator, &create_info, &alloc_info, &vk_image, &allocation, nullptr);

	if (result != VK_SUCCESS)
		return result;

	VkImageViewCreateInfo view_info {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.flags = 0, 
		.image = vk_image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = create_info.format,
		.components = {},
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		}
	};

	VkImageView view = VK_NULL_HANDLE;

	result = vkCreateImageView(ctx->device, &view_info, nullptr, &view);

	Image image = {
		.image = vk_image,
		.allocation = allocation,
		.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
		.w = w,
		.h = h,
		.d = 1,
		.levels = 1,
	};

	ImageID handle = ctx->emplace_image(std::move(image));

	*out_image = handle;
	*out_view = view;

	return result;
}

//------------------------------------------------------------------------------
// Interface

RenderTarget *create_render_target_internal(
	GfxContext *ctx,
	uint32_t w, uint32_t h,
	VkImageView color_view,
	VkImageView depth_view,
	RenderTargetFlags flags
)
{
	RenderTarget *target = new RenderTarget{
		.w = w,
		.h = h,
		.flags = flags,
	};

	VkResult result = VK_SUCCESS; 

	if (flags & RENDER_TARGET_CREATE_DEPTH_BIT) {
		if (depth_view) {
			log_error(
				"RENDER_TARGET_CREATE_DEPTH_BIT set while depth_view is not VK_NULL_HANDLE"); 
			return nullptr;
		}
		result = create_depth_stencil_image(ctx, w, h, &target->depth_img, &target->depth_view);
		if (result != VK_SUCCESS)
			goto cleanup;
	} else {
		target->depth_view = depth_view;
	}

	if (flags & RENDER_TARGET_CREATE_COLOR_BIT) {
		if (color_view) {
			log_error(
				"RENDER_TARGET_CREATE_COLOR_BIT set while and color_view is not VK_NULL_HANDLE"); 
			return nullptr;
		}
		result = create_color_image(ctx, w, h, &target->color_img, &target->color_view);
		if (result != VK_SUCCESS)
			goto cleanup;
	} else {
		target->color_view = color_view;
	}

	return target;
cleanup:
	destroy_render_target_internal(ctx, target);
	return {};
}

void destroy_render_target_internal(
	GfxContext *ctx, 
	RenderTarget* target
)
{
	if (target->depth_img.is_valid() && target->flags & RENDER_TARGET_CREATE_DEPTH_BIT) {
		if (target->depth_img.is_valid())
			destroy_image(ctx, target->depth_img);
		if (target->depth_view)
			vkDestroyImageView(ctx->device, target->depth_view, nullptr);
	}

	if (target->flags & RENDER_TARGET_CREATE_COLOR_BIT) {
		if (target->color_img.is_valid())
			destroy_image(ctx, target->color_img);
		if (target->color_view)
			vkDestroyImageView(ctx->device, target->color_view, nullptr);
	}

	delete target;
}

RenderTargetID create_render_target(
	GfxContext *ctx, 
	uint32_t w, 
	uint32_t h, 
	RenderTargetFlags flags
)
{
	RenderTarget * target = create_render_target_internal(
		ctx, 
		w, h, 
		VK_NULL_HANDLE, 
		VK_NULL_HANDLE, 
		flags
	);

	return EV2_HANDLE_CAST(RenderTarget, target);
}

void destroy_render_target(
	GfxContext *ctx, 
	RenderTargetID h
)
{
	RenderTarget *target = EV2_TYPE_PTR_CAST(RenderTarget, h);
	destroy_render_target_internal(ctx, target);
}

VkImageView get_render_target_color_view(RenderTargetID handle)
{
	RenderTarget *target = EV2_TYPE_PTR_CAST(RenderTarget, handle);
	return target->color_view;
}

ImageID get_render_target_color_image(RenderTargetID handle)
{
	RenderTarget *target = EV2_TYPE_PTR_CAST(RenderTarget, handle);
	return target->color_img;
}

ViewID create_view(GfxContext *ctx, float view[], float proj[])
{
	ViewData data = view_data_from_matrices(view, proj);
	uint32_t id = ctx->view_data.add(data);

	return EV2_HANDLE_CAST(View, id);
}

void update_view(GfxContext *ctx, ViewID handle, float view[], float proj[])
{
	ViewData data = view_data_from_matrices(view, proj);

	uint32_t id = static_cast<uint32_t>(handle.id);

	if (!ctx->view_data.set(id, data))
		log_error("ViewID %d is invalid",id);
}

void destroy_view(GfxContext *ctx, ViewID handle)
{
	uint32_t id = static_cast<uint32_t>(handle.id);
	ctx->view_data.remove(id);
}

static void populate_rendering_info(
	const RenderTarget *p_target,
	VkRenderingInfo *p_rendering_info,
	VkRenderingAttachmentInfo *p_color_info,
	VkRenderingAttachmentInfo *p_depth_info,
	VkRenderingAttachmentInfo *p_stencil_info
)
{
	static float rgb[3] = {0.0f,0.0f,0.0f}; bool has_color = p_target->color_view != VK_NULL_HANDLE;
	bool has_depth = p_target->depth_view != VK_NULL_HANDLE;
	bool has_stencil = has_depth;

	if (has_color) {
		*p_color_info = VkRenderingAttachmentInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = p_target->color_view,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
			.resolveMode = VK_RESOLVE_MODE_NONE,
			.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue = VkClearValue{
				.color = {
					.float32 = {rgb[0],rgb[0],rgb[0],1.f}
				}
			},
		};
	}

	if (has_depth) {
		*p_depth_info = VkRenderingAttachmentInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = p_target->depth_view,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
			.resolveMode = VK_RESOLVE_MODE_NONE,
			.resolveImageView = VK_NULL_HANDLE,
			.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue = VkClearValue{
				.depthStencil = {
					.depth = 1.f,
					.stencil = 0
				}
			},
		};
	}
	if (has_stencil) {
		*p_stencil_info = VkRenderingAttachmentInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = p_target->depth_view,
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
			.resolveMode = VK_RESOLVE_MODE_NONE,
			.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.clearValue = VkClearValue{
				.depthStencil = {
					.depth = 0,
					.stencil = 0
				}
			},
		};
	}

	*p_rendering_info = VkRenderingInfo{
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.flags = 0,
		.renderArea = VkRect2D{
			.offset = {.x = 0, .y = 0,},
			.extent = {.width = p_target->w, .height = p_target->h}
		},
		.layerCount = 1,
		.viewMask = 0,
		.colorAttachmentCount = 1,
		.pColorAttachments = has_color ? p_color_info : nullptr,
		.pDepthAttachment = has_depth ? p_depth_info : nullptr,
		.pStencilAttachment = has_stencil ? p_stencil_info : nullptr,
	};
}

PassID begin_gfx_pass(
	GfxContext *ctx, 
	RenderTargetID target_handle, ViewID view_handle,
	Rect in_viewport, Rect in_scissor
)
{
	ctx->assert_inside_frame();

	if (EV2_IS_NULL(ctx->view_data.buffer))
		log_error("No views created.  Did you remember to call begin_frame()?"); 

	if (view_handle.id == 0) {
		view_handle = ctx->default_view;
	}

	FrameContext *frame = ctx->get_current_frame();

	Pass *pass = frame->new_pass(Pass{
		.ctx = ctx,
		.queue_family = ctx->graphics_family->index
	});

	Rect scissor = in_scissor;

	if (in_scissor.h == 0 || in_scissor.w == 0) {
		scissor = in_viewport;
	}

	std::unique_ptr<RenderPass> gfx (new RenderPass{
		.target = target_handle,
		.view = view_handle,
		.viewport = in_viewport,
		.scissor = scissor
	});

	VkDescriptorSetLayout layout = 
		ctx->get_base_descriptor_set_layout(EV2_BASE_SET_PER_PASS); 

	VkDescriptorSetAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = frame->descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &layout,
	};

	VkResult result = 
		vkAllocateDescriptorSets(ctx->device, &alloc_info, &gfx->descriptor_set);

	if (result != VK_SUCCESS)
		return EV2_NULL_HANDLE(Pass);

	Buffer * buf = ctx->get_buffer(ctx->view_data.buffer);

	VkDescriptorBufferInfo buffer_info = {
		.buffer = buf->buffer,
		.offset = ctx->view_data.get_offset((uint32_t)view_handle.id),
		.range = ctx->view_data.stride
	};

	VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = gfx->descriptor_set,
		.dstBinding = 0,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &buffer_info
	};

	vkUpdateDescriptorSets(ctx->device, 1, &write, 0, nullptr);

	if (!gfx)
		return EV2_NULL_HANDLE(Pass);

	pass->gfx = std::move(gfx);

	const bool render_to_swapchain = !target_handle.is_valid();

	if (render_to_swapchain) {
		pass->cmds.push_back(Command{
			.use_image = CmdUseImage{
				.image = ctx->depth_buffer,
				.usage = USAGE_DEPTH_ATTACHMENT, 
			},
			.type = UseImage
		});		
	} else if (RenderTarget *target = EV2_TYPE_PTR_CAST(RenderTarget,target_handle)){
		if (target->depth_img.is_valid())
			pass->cmds.push_back(Command{
				.use_image = CmdUseImage{
					.image = target->depth_img,
					.usage = USAGE_DEPTH_ATTACHMENT, 
				},
				.type = UseImage
			});		
		if (target->color_img.is_valid())
			pass->cmds.push_back(Command{
				.use_image = CmdUseImage{
					.image = target->color_img,
					.usage = USAGE_COLOR_ATTACHMENT, 
				},
				.type = UseImage
			});		
	}

	return EV2_HANDLE_CAST(Pass, pass); 
}

PassID begin_compute_pass(GfxContext *ctx)
{
	ctx->assert_inside_frame();

	FrameContext *frame = ctx->get_current_frame();
	Pass *pass = frame->new_pass(Pass{
		.ctx = ctx,
		.queue_family = ctx->graphics_family->index
	});

	return EV2_HANDLE_CAST(Pass, pass); 

}

void end_pass(GfxContext *ctx, PassID pass_handle)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_handle);
}

static bool access_contains_write(VkAccessFlags2 access)
{
    constexpr VkAccessFlags2 writeMask =
        VK_ACCESS_2_SHADER_WRITE_BIT |
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_2_TRANSFER_WRITE_BIT |
        VK_ACCESS_2_HOST_WRITE_BIT |
        VK_ACCESS_2_MEMORY_WRITE_BIT |
        VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR |
        VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT |
        VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT |
        VK_ACCESS_2_COMMAND_PREPROCESS_WRITE_BIT_NV |
        VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR |
        VK_ACCESS_2_VIDEO_ENCODE_WRITE_BIT_KHR |
        VK_ACCESS_2_OPTICAL_FLOW_WRITE_BIT_NV;

    return (access & writeMask) != 0;
}

static ResourceStateFlags usage_to_state_flags(Usage usage)
{
	switch(usage) {
		case USAGE_TRANSFER_SRC:
			return ResourceStateFlags{
				.access = VK_ACCESS_2_TRANSFER_READ_BIT, 
				.stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT
			};
		case USAGE_TRANSFER_DST:
			return ResourceStateFlags{
				.access = VK_ACCESS_2_TRANSFER_WRITE_BIT, 
				.stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT
			};
		case USAGE_SAMPLED_GRAPHICS:
			return ResourceStateFlags{
				.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, 
				.stage = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
			};
		case USAGE_COLOR_ATTACHMENT:
			return ResourceStateFlags{
				.access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 
				.stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
			};
		case USAGE_DEPTH_ATTACHMENT:
			return ResourceStateFlags{
				.access = 
					VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
					VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, 
				.stage = 
					VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT_KHR |
					VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT_KHR
			};
		case USAGE_STORAGE_READ_COMPUTE:
			return ResourceStateFlags{
				.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
				.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
			};
		case USAGE_STORAGE_RW_COMPUTE:
			return ResourceStateFlags{
				.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
				.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
			};
		case USAGE_INDEX_INPUT:
			return ResourceStateFlags{
				.access = VK_ACCESS_2_INDEX_READ_BIT,
				.stage = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
			};
		case USAGE_VERTEX_INPUT:
			return ResourceStateFlags{
				.access = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
				.stage = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
			};
		case USAGE_MAX_ENUM:
		case USAGE_UNDEFINED:
		default:
			return {};
	}
	return {};
}

enum PassReadWrite{
	PASS_READ = 0x1,
	PASS_WRITE = 0x2,
	PASS_RW = 0x3,
};

static uint8_t usage_to_rw_flags(Usage usage)
{
	switch(usage) {
		case USAGE_TRANSFER_SRC:
			return PASS_READ;
		case USAGE_TRANSFER_DST:
			return PASS_WRITE;
		case USAGE_SAMPLED_GRAPHICS:
			return PASS_READ;
		case USAGE_COLOR_ATTACHMENT:
			return PASS_WRITE;
		case USAGE_DEPTH_ATTACHMENT:
			return PASS_WRITE | PASS_READ;
		case USAGE_STORAGE_READ_COMPUTE:
			return PASS_READ;
		case USAGE_STORAGE_RW_COMPUTE:
			return PASS_READ | PASS_WRITE;
		case USAGE_MAX_ENUM:
		case USAGE_UNDEFINED:
		default:
			return {};
			break;
	}
	return {};
}

static ResourceStateFlags update_resource_state(ResourceState *state, Usage usage, uint32_t queue_family)
{
	ResourceStateFlags flags = usage_to_state_flags(usage);
	uint8_t rw = usage_to_rw_flags(usage);

	if (rw & PASS_WRITE) {
		return state->set_write(flags.access, flags.stage, queue_family);
	}
	else if (rw & PASS_READ) {
		return state->set_read(flags.access, flags.stage, queue_family);
	}

	return {};
}

void cmd_use_buffer(
	PassID pass_id,
	BufferID buf_id,
	Usage usage
)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_id);

	pass->cmds.push_back(Command{
		.use_buffer = {
			.buffer = buf_id,
			.usage = usage
		},
		.type = UseBuffer,
	});
}

void cmd_use_image(
	PassID pass_id,
	ImageID img_id,
	Usage usage
)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_id);

	pass->cmds.push_back(Command{
		.use_image = {
			.image = img_id,
			.usage = usage
		},
		.type = UseImage,
	});
}

void cmd_bind_resources(PassID pass_id, BindingsID bindings_id)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_id);

	Bindings *bindings = pass->ctx->get_bindings(bindings_id);

	if (!bindings->writes.empty()) {
		log_warn("Binding resources with pending writes. Remember to call flush_bindings");
	}

	pass->cmds.push_back(Command{
		.bind_resources = {
			.bindings = bindings_id
		},
		.type = BindResources,
	});
}

void cmd_bind_compute_pipeline(PassID pass_id, ComputePipelineID pipeline_id)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_id);
	ComputePipeline *pipeline = pass->ctx->get_compute_pipeline(pipeline_id);

	if (!pipeline) {
		log_error("Invalid pipeline handle %lld", (unsigned long long)pipeline_id.id);
		return;
	}

	pass->cmds.push_back(Command{
		.bind_compute_pipeline = {
			.pipeline = pipeline_id
		},
		.type = BindComputePipeline,
	});
}

static void cmd_push_constant_internal(
	Pass* pass, 
	BasePipeline *pipeline, 
	uint32_t offset, 
	uint32_t size, 
	void *data
)
{
	uint32_t max_size = pass->ctx->caps.limits.maxPushConstantsSize; 

	if (offset & 0x3) {
		log_error("Push constant byte offset must be a multiple of 4");
		return;
	}

	if (size & 0x3) {
		log_error("Push constant size must be a multiple of 4");
		return;
	}

	if (offset >= max_size) {
		log_error("Push constant byte offset must be less than %d (offset=%d)", 
			max_size, offset);
		return;
	}

	if (size > max_size - offset) {
		log_error("Push constant size exceed range limits (offset=%d, size=%d, max=%d",
			offset, size, max_size);
		return;
	}

	uint32_t src_offset = pass->push_constant_data.size();

	pass->push_constant_data.push_range((char*)data, size);
	pass->cmds.push_back(Command{
		.push_constant = CmdPushConstant{
			.pipeline = pipeline,
			.src_offset = src_offset,
			.offset = offset,
			.size = size,
		},
		.type = PushConstant
	});
}

void cmd_push_constant(
	PassID pass_id, 
	ComputePipelineID pipeline_id, 
	uint32_t offset, 
	uint32_t size, 
	void *data
)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_id);
	ComputePipeline *pipeline = pass->ctx->get_compute_pipeline(pipeline_id);
	cmd_push_constant_internal(pass, &pipeline->base, offset, size, data);
}

void cmd_push_constant(
	PassID pass_id, 
	GfxPipelineID pipeline_id, 
	uint32_t offset, 
	uint32_t size, 
	void *data
)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_id);
	GfxPipeline *pipeline = pass->ctx->get_gfx_pipeline(pipeline_id);
	cmd_push_constant_internal(pass, &pipeline->base, offset, size, data);
}

void cmd_dispatch(
	PassID pass_id,
	uint32_t countx, 
	uint32_t county, 
	uint32_t countz
)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_id);

	pass->cmds.push_back(Command{
		.dispatch = {
			.counts = {
				countx, county, countz
			}
		},
		.type = Dispatch,
	});
}

void cmd_custom(
	PassID pass_id,
	std::function<void(VkCommandBuffer)>&& callback
) 
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_id);

	pass->cmds.push_back(Command{
		.custom = CmdCustom{
			.callback_id = (uint32_t)pass->custom_callbacks.size()
		},
		.type = Custom,
	});
	pass->custom_callbacks.push_back(std::move(callback));
}

void cmd_bind_gfx_pipeline(PassID pass_id, GfxPipelineID pipeline_id)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_id);
	GfxPipeline *pipeline = pass->ctx->get_gfx_pipeline(pipeline_id);

	if (!pipeline) {
		log_error("Invalid pipeline handle %lld", (unsigned long long)pipeline_id.id);
		return;
	}

	pass->cmds.push_back(Command{
		.bind_gfx_pipeline = {
			.pipeline = pipeline_id
		},
		.type = BindGfxPipeline,
	});
}

void cmd_bind_index_buffer(PassID pass_id, BufferID buf_id, size_t offset)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_id);
	pass->cmds.push_back(Command{
		.bind_index_buffer = CmdBindIndexBuffer{
			.buffer = buf_id,
			.offset = offset
		},
		.type = BindIndexBuffer,
	});
}

void cmd_bind_vertex_buffer(PassID pass_id, BufferID buf_id, size_t offset)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_id);
	pass->cmds.push_back(Command{
		.bind_vertex_buffer = CmdBindVertexBuffer{
			.buffer = buf_id,
			.offset = offset
		},
		.type = BindVertexBuffer,
	});
}

void cmd_bind_indirect_buffer(PassID pass_id, BufferID buf_id, size_t offset)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_id);
	pass->cmds.push_back(Command{
		.bind_indirect_buffer = CmdBindIndirectBuffer{
			.buffer = buf_id,
			.offset = offset
		},
		.type = BindIndirectBuffer,
	});
}

//------------------------------------------------------------------------------
// Render graph compilation

VkResult TransientCommands::init(VkDevice _device, uint32_t queue_family_index)
{
	this->device = _device;
	VkCommandPoolCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = queue_family_index,
	};

	VkResult result = vkCreateCommandPool(device, &create_info, nullptr, &command_pool);
	return result;
}
void TransientCommands::destroy()
{
	vkDestroyCommandPool(device, command_pool, nullptr);
}

VkResult TransientCommands::reset()
{
	VkResult result = vkResetCommandPool(device, command_pool, 0);

	if (result != VK_SUCCESS)
		return result;

	for (VkCommandBuffer cmds : in_use_cmds) {
		free_cmds.push_back(cmds);
	}
	in_use_cmds.clear();

	return result;
}

VkResult TransientCommands::get_cmds(uint32_t count, VkCommandBuffer *out_cmds)
{
	VkResult result = VK_SUCCESS; 

	uint32_t free_count = (uint32_t)free_cmds.size(); 
	if (count > free_count) {
		VkCommandBufferAllocateInfo alloc_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = command_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = count - free_count,
		};

		result = vkAllocateCommandBuffers(device, &alloc_info, &out_cmds[free_count]);
		if (result != VK_SUCCESS)
			return result;

		if (free_count)
			std::copy(free_cmds.begin(), free_cmds.end(), out_cmds);   

		free_cmds.clear();
	} else {
		for (uint32_t i = 0; i < count; ++i) {
			VkCommandBuffer cmds = free_cmds.back();
			free_cmds.pop_back();
			out_cmds[i] = cmds;
		}
	}

	for (uint32_t i = 0; i < count; ++i) {
		in_use_cmds.push_back(out_cmds[i]);
	}

	return result;
}

Result begin_frame(GfxContext *ctx)
{
	FrameContext *frame = ctx->get_current_frame();
	uint64_t old_frame_index = frame->index;
	Result result = ev2::SUCCESS; 

	if (!ctx->is_swap_chain_valid)
		result = ctx->reset_swap_chain();
	
	if (result != ev2::SUCCESS)
		return result;

	if (ctx->frame_counter >= ctx->max_frames_in_flight)
		result = ctx->wait_for_frame(old_frame_index);


	//----------------------------------------------------------------------------
	// Manage timing

	uint32_t interval_ns = 1000000000/ctx->framerate_hz;

	struct timespec ts_prev = ctx->last_frame_ts; 
	struct timespec ts_wait = {
		.tv_sec = ts_prev.tv_sec,
		.tv_nsec = ts_prev.tv_nsec + interval_ns 
	};

	if (ts_wait.tv_nsec >= 1000000000) {
		++ts_wait.tv_sec;
		ts_wait.tv_nsec -= 1000000000;
	}

	result = platform::sleep_until(&ts_wait);
	if (result != ev2::SUCCESS)
		return result;

	struct timespec ts_now = platform::monotonic_clock_time();
	ctx->last_frame_ts = ts_now;

	double now_sec = ctx->seconds_since_start(ts_now);

	frame->dt = now_sec - ctx->seconds_since_start(ts_prev);
	frame->t = now_sec;

	if (result != ev2::SUCCESS)
		return result;

	//----------------------------------------------------------------------------
	// Acquire swapchain image
	
	constexpr uint64_t timeout_ns = 1e9;

	VkAcquireNextImageInfoKHR acquire_info = {
		.sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
		.swapchain = ctx->swap_chain.swapchain,
		.timeout = timeout_ns,
		.semaphore = frame->image_available_sempahore,
		.fence = VK_NULL_HANDLE,
		.deviceMask = (uint32_t)(1 << ctx->physical_device_index),
	};

	uint32_t image_index;
    VkResult vk_result = vkAcquireNextImage2KHR(ctx->device, &acquire_info, &image_index);

	if (vk_result == VK_ERROR_OUT_OF_DATE_KHR) {
        result = ctx->reset_swap_chain();
    } else if (vk_result != VK_SUCCESS && vk_result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

	if (result != SUCCESS) {
		return result;
	}

	//------------------------------------------------------------------------------
	// Cleanup frame context if previously used
	
	if (old_frame_index >= ctx->max_frames_in_flight) {
		result = reset_frame_context(ctx, frame);

		if (result != SUCCESS)
			return set_error(result, "Failed to reset frame context");
	}

	frame->index = ctx->frame_counter;
	frame->image_index = image_index; frame->image_use_count = 0;
	frame->screen_target = ctx->swap_chain.targets[image_index];
	frame->render_finished_semaphore = ctx->swap_chain.semaphores[image_index];

	//------------------------------------------------------------------------------
	// Update stuff

	if (ctx->assets->reloader) {
		ctx->assets->reloader->update();
	}
	ctx->transforms.update(ctx);
	ctx->view_data.update(ctx);

	GPUFramedata gpu_data = {
		.t_seconds = (uint32_t)frame->t,
		.t_fract = (float)fmod(frame->t, 1.),
		.dt = (float)frame->dt
	};

	UploadContext uc = begin_upload(ctx, sizeof(GPUFramedata), alignof(GPUFramedata));
	memcpy(uc.ptr, &gpu_data, sizeof(GPUFramedata));
	BufferUpload up = {
		.src_offset = 0,
		.dst_offset = 0,
		.size = sizeof(GPUFramedata),
	};
	commit_buffer_uploads(ctx, uc, frame->ubo, &up, 1);
	flush_uploads(ctx);

	return SUCCESS;
}

static void rg_use_buffer(RenderGraph &rg, GfxContext *ctx, uint32_t pass_idx, const CmdUseBuffer &cmd);
static void rg_use_image(RenderGraph &rg, GfxContext *ctx, uint32_t pass_idx, const CmdUseImage &cmd);

static PassEdge *get_or_insert_edge(RenderGraph *rg, const PassEdge& edge)
{
	auto [it, inserted] = rg->edge_idx_map.emplace(edge.get_key(), rg->edges.size());

	if (inserted) {
		rg->edges.push_back(edge);
	}

	return &rg->edges.back();
}

static void update_barriers_for_use(
	RenderGraph &rg,
	uint32_t src_node_idx, 
	uint32_t dst_node_idx,
	ResourceState *state, 
	TaggedResource resource, 
	Usage usage
)
{
	state->last_used_by_frame = rg.ctx->frame_counter;

	PassNode &dst_node = rg.nodes[dst_node_idx];


	ResourceStateFlags src_flags = update_resource_state(state, usage, dst_node.queue_family_index);
	ResourceStateFlags dst_flags = state->get_current();

	const bool is_write_barrier = (bool)(usage_to_rw_flags(usage) & PASS_WRITE);

	PassBarrier barrier = {
		.src_state = src_flags,
		.dst_state = dst_flags,
		.resource = resource,
		.is_write = is_write_barrier 
	};

	bool needs_queue_transfer = 
		barrier.dst_state.queue_family_index != barrier.src_state.queue_family_index; 

	// TODO: Handle situation where resource had been used by another queue family 
	// not in this frame.

	if (needs_queue_transfer) {
		if (src_node_idx != PASS_NODE_INDEX_OUT_OF_FRAME) {
			rg.nodes[src_node_idx].barriers.push_back(barrier);
		} else {
			const char *type_str = "";
			switch(resource.type) {
				case RESOURCE_TYPE_BUFFER: type_str = "Buffer"; break;
				case RESOURCE_TYPE_IMAGE: type_str = "Image"; break;
				default: break;
			}

			log_warn(
				"%s %d acquired on queue family %d without release on queue family %d",
				type_str, resource.handle, 
				barrier.dst_state.queue_family_index, barrier.src_state.queue_family_index
			); 
		}
	}

	PassEdge *edge = get_or_insert_edge(&rg, PassEdge{
		.src_node = src_node_idx,
		.dst_node = dst_node_idx,
		.src_state = src_flags,
		.dst_state = dst_flags,
		.resource = resource,
		.src_write = access_contains_write(src_flags.access),
	});

	// When there was previously a read barrier on the resource, and we're
	// doing another read, we will union the destination flags of the existing
	// barrier with the new destination flags instead of adding a redundant
	// barrier.

	PassBarrier *final_barrier;
	if (
		final_barrier = dst_node.barriers.empty() ? nullptr : &dst_node.barriers.back();

		!is_write_barrier && final_barrier && 
		!final_barrier->is_write && final_barrier->resource == resource
	) {
		final_barrier->dst_state.access |= barrier.dst_state.access;
		final_barrier->dst_state.stage |= barrier.dst_state.stage;

		// update the barrier for the edge
		edge->dst_state = final_barrier->dst_state;
		edge->src_state = final_barrier->src_state;
	} else {
		dst_node.barriers.push_back(barrier);
		final_barrier = &dst_node.barriers.back();
	}

	edge->dst_write = is_write_barrier,
	dst_node.final_states[resource] = final_barrier->dst_state;
}

void rg_use_buffer(RenderGraph &rg, GfxContext *ctx, uint32_t dst_node_idx, const CmdUseBuffer& cmd)
{
	auto [it, inserted] = rg.buffer_passes.emplace(cmd.buffer.id, dst_node_idx);

	Buffer *buf = ctx->get_buffer(cmd.buffer);
	uint32_t src_node_idx = inserted ? PASS_NODE_INDEX_OUT_OF_FRAME : it->second;
	if (!inserted)
		it->second = dst_node_idx;

	update_barriers_for_use(rg, src_node_idx, dst_node_idx, &buf->state, cmd.buffer, cmd.usage);
}

void rg_use_image(RenderGraph &rg, GfxContext *ctx, uint32_t dst_node_idx, const CmdUseImage& cmd)
{
	auto [it, inserted] = rg.image_passes.emplace(cmd.image.id, dst_node_idx);

	Image *img = ctx->get_image(cmd.image);
	uint32_t src_node_idx = inserted ? PASS_NODE_INDEX_OUT_OF_FRAME : it->second;
	if (!inserted)
		it->second = dst_node_idx;

	ResourceState *state = &img->state;

	update_barriers_for_use(rg, src_node_idx, dst_node_idx, state, cmd.image, cmd.usage);

	assert(state->get_current().layout != VK_IMAGE_LAYOUT_UNDEFINED);
}

static PassCommand &rg_new_pass_command(PassNode &node, const Command cmd)
{
	uint32_t barrier_count = (uint32_t)node.barriers.size();
	uint32_t barrier_offset = node.barrier_offset;

	uint32_t cmd_barrier_count = barrier_count > barrier_offset ? 
		barrier_count - barrier_offset : 0;

	node.barrier_offset = barrier_count;

	return node.commands.emplace_back(PassCommand{
		.base = cmd,
		.barrier_count = cmd_barrier_count
	});
}

static ev2::Result rg_compile(GfxContext *ctx, RenderGraph *rg)
{
	FrameContext *frame = ctx->get_current_frame();

	uint32_t node_count = (uint32_t)rg->nodes.size();

	for (uint32_t node_idx = 0; node_idx < node_count; ++node_idx) {
		PassNode &node = rg->nodes[node_idx];
		const Pass *pass = node.pass;

		uint32_t cmd_count = (uint32_t)pass->cmds.size();

		for (uint32_t i = 0; i < cmd_count; ++i) {
			const Command &cmd = pass->cmds[i];

			switch(cmd.type) {
				case BindComputePipeline:
				case BindGfxPipeline:
				case BindResources:
				case BindVertexBuffer:
				case BindIndexBuffer:
				case BindIndirectBuffer:
				case PushConstant:
				case Custom:
				case Dispatch: {
					rg_new_pass_command(node, cmd);
					break;
				}
				case UseBuffer: {
					rg_use_buffer(*rg, ctx, node_idx, cmd.use_buffer);
					break;
				} 
				case UseImage: {
					rg_use_image(*rg, ctx, node_idx, cmd.use_image);
					break;
				}
			}
		}
	}

	//------------------------------------------------------------------------------
	// Assign submission groups
	//
	// Naive approach: For starters, just assign every node it's own submission group;
	// TODO: Merge sequential node dependencies into a single command buffer
	
	uint32_t last_screen_submission_index = INDEX_NONE;
	PassNode *last_screen_node = nullptr;

	for (PassNode &node : rg->nodes) {
		uint32_t queue_family_index = node.pass->queue_family;

		RenderGraphSubmission &submission = rg->submissions.emplace_back(
			RenderGraphSubmission{
				.nodes = {&node},
				.queue_index = queue_family_index
			});

		uint32_t submission_index = (uint32_t)rg->submissions.size() - 1;

		// Add image layout transition barrier for swapchain image, and
		// wait on image available semaphore 
		if (node.pass->gfx && node.pass->gfx->render_to_swapchain()) {
			if (!frame->image_use_count++) {
				ImageID swap_img_id = 
					ctx->swap_chain.image_ids[frame->image_index]; 

				node.pre_barriers.push_back(PassBarrier{
					.src_state = ResourceStateFlags{
						.access = VK_ACCESS_2_NONE,
						.stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
						.queue_family_index = VK_QUEUE_FAMILY_IGNORED,
						.layout = VK_IMAGE_LAYOUT_UNDEFINED,
					},
					.dst_state = ResourceStateFlags{
						.access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
						.stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
						.queue_family_index = VK_QUEUE_FAMILY_IGNORED,
						.layout = VK_IMAGE_LAYOUT_GENERAL,
					},
					.resource = swap_img_id,
					.is_write = true,
				});

				submission.wait.push_back(VkSemaphoreSubmitInfo{
					.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
					.semaphore = frame->image_available_sempahore,
					.stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					.deviceIndex = DEFAULT_DEVICE_INDEX,
				});
			}
			last_screen_submission_index = submission_index;
			last_screen_node = &node;
		}
	}

	//-----------------------------------------------------------------------------
	// Necessary synchronization for swapchain

	if (last_screen_node) {
		ImageID swap_img_id = 
			ctx->swap_chain.image_ids[frame->image_index]; 
		last_screen_node->post_barriers.push_back(PassBarrier{
			.src_state = ResourceStateFlags{
				.access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
				.stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				.queue_family_index = VK_QUEUE_FAMILY_IGNORED,
				.layout = VK_IMAGE_LAYOUT_GENERAL,
			},
			.dst_state = ResourceStateFlags{
				.access = VK_ACCESS_2_NONE,
				.stage = VK_PIPELINE_STAGE_2_NONE,
				.queue_family_index = VK_QUEUE_FAMILY_IGNORED,
				.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			},
			.resource = swap_img_id,
			.is_write = true,
		});
	}

	if (last_screen_submission_index != INDEX_NONE) {
		rg->submissions[last_screen_submission_index].signal.push_back(VkSemaphoreSubmitInfo{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = frame->render_finished_semaphore,
			.stageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			.deviceIndex = DEFAULT_DEVICE_INDEX,
		});
		rg->submissions[last_screen_submission_index].signal.push_back(VkSemaphoreSubmitInfo{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = ctx->frame_semaphore,
			.value = 1 + frame->index,
			.stageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			.deviceIndex = DEFAULT_DEVICE_INDEX,
		});
	} else {
		return set_error(ev2::ERENDER_GRAPH, "Incomplete render graph: does not render to display");
	}

	//------------------------------------------------------------------------------
	// Populate semaphores

	for (const PassEdge &edge : rg->edges) {
		const bool is_cross_queue_edge = edge.is_cross_queue(); 

		ResourceState *state = nullptr;
		switch (edge.resource.type) {
			case RESOURCE_TYPE_BUFFER: {
				Buffer *buffer = ctx->get_buffer(edge.resource.to_buffer());
				state = &buffer->state;
				break;
			}
			case RESOURCE_TYPE_IMAGE: {
				Image *image = ctx->get_image(edge.resource.to_image());
				state = &image->state;
				break;
			}
		}
		assert(state);

		PassNode &dst_node = rg->nodes[edge.dst_node];
		RenderGraphSubmission &dst_submission = rg->submissions[dst_node.submission_idx];
		
		ResourceSync *sync = nullptr;

		if (edge.src_node == PASS_NODE_INDEX_OUT_OF_FRAME) {
			sync = frame->get_resource_sync(
				edge.resource, 
				rg->queues[dst_submission.queue_index]->queue
			);
		} else if (is_cross_queue_edge) {
			PassNode &src_node = rg->nodes[edge.src_node];
			RenderGraphSubmission &src_submission = rg->submissions[src_node.submission_idx];

			sync = frame->get_resource_sync(
				edge.resource, 
				rg->queues[src_submission.queue_index]->queue
			);

			++sync->wait_value;

			src_submission.signal.push_back(VkSemaphoreSubmitInfo{
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				.semaphore = sync->semaphore,
				.value = sync->wait_value,
				.stageMask = edge.src_state.stage,
				.deviceIndex = DEFAULT_DEVICE_INDEX,
			});
		}

		if (sync) {
			dst_submission.wait.push_back(VkSemaphoreSubmitInfo{
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				.semaphore = sync->semaphore,
				.value = sync->wait_value,
				.stageMask = edge.dst_state.stage,
				.deviceIndex = DEFAULT_DEVICE_INDEX,
			});
			if (edge.src_write) {
				state->sync_write(sync->semaphore, sync->wait_value);
			} else {
				state->sync_read(sync->semaphore, sync->wait_value);
			}
		}
	}

	return ev2::SUCCESS;
}

static VkImageMemoryBarrier2 pass_barrier_to_vk_image(GfxContext *ctx, const PassBarrier &barrier)
{
	const Image *image = ctx->get_image(barrier.resource.to_image());
	assert(barrier.resource.type == RESOURCE_TYPE_IMAGE);

	return VkImageMemoryBarrier2{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,

		.srcStageMask = barrier.src_state.stage,
		.srcAccessMask = barrier.src_state.access,

		.dstStageMask = barrier.dst_state.stage,
		.dstAccessMask = barrier.dst_state.access,

		.oldLayout = barrier.src_state.layout,
		.newLayout = barrier.dst_state.layout,

		.srcQueueFamilyIndex = barrier.is_queue_transfer() ?
			barrier.src_state.queue_family_index : VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = barrier.is_queue_transfer() ?
			barrier.dst_state.queue_family_index : VK_QUEUE_FAMILY_IGNORED,

		.image = image->image,
		.subresourceRange = VkImageSubresourceRange{
			.aspectMask = image->aspect_mask,
			.baseMipLevel = 0,
			.levelCount = image->levels,
			.baseArrayLayer = 0,
			.layerCount = image->d,
		}
	};
}

static VkBufferMemoryBarrier2 pass_barrier_to_vk_buffer(GfxContext *ctx, const PassBarrier &barrier)
{
	const Buffer *buffer = ctx->get_buffer(barrier.resource.to_buffer());
	assert(barrier.resource.type == RESOURCE_TYPE_BUFFER);

	return VkBufferMemoryBarrier2{
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,

		.srcStageMask = barrier.src_state.stage,
		.srcAccessMask = barrier.src_state.access,

		.dstStageMask = barrier.dst_state.stage,
		.dstAccessMask = barrier.dst_state.access,

		.srcQueueFamilyIndex = barrier.is_queue_transfer() ?
			barrier.src_state.queue_family_index : VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = barrier.is_queue_transfer() ?
			barrier.dst_state.queue_family_index : VK_QUEUE_FAMILY_IGNORED,

		.buffer = buffer->buffer,
		.offset = 0,
		.size = buffer->size
	};
}

static void translate_barriers(
	GfxContext *ctx,
	const PassBarrier *barriers,
	uint32_t count,
	std::vector<VkBufferMemoryBarrier2>& buffer_barriers,
	std::vector<VkImageMemoryBarrier2>& image_barriers
)
{
	for (const PassBarrier *p_barrier = barriers; p_barrier < barriers + count; ++p_barrier) {
		const PassBarrier &barrier = *p_barrier;

		switch (barrier.resource.type) {
			case RESOURCE_TYPE_BUFFER: {
				buffer_barriers.push_back(pass_barrier_to_vk_buffer(ctx, barrier));
				break; 
			}
			case RESOURCE_TYPE_IMAGE: {
				image_barriers.push_back(pass_barrier_to_vk_image(ctx, barrier));
				break;
			}
		}
	}
}

static VkDependencyInfo generate_dependency_info(
	GfxContext *ctx,
	std::vector<VkBufferMemoryBarrier2>& buffer_barriers,
	std::vector<VkImageMemoryBarrier2>& image_barriers
)
{
	return VkDependencyInfo{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.dependencyFlags = 0,
		.bufferMemoryBarrierCount = (uint32_t)buffer_barriers.size(),
		.pBufferMemoryBarriers = buffer_barriers.data(),
		.imageMemoryBarrierCount = (uint32_t)image_barriers.size(),
		.pImageMemoryBarriers = image_barriers.data(),

	};
}

static void rg_record_gfx_pass_begin(RenderGraph*rg, const PassNode &node, VkCommandBuffer cmds)
{
	GfxContext *ctx = rg->ctx;

	const FrameContext *frame = ctx->get_current_frame(); 
	const RenderPass *render_pass = node.pass->gfx.get();

	assert(render_pass);

	//------------------------------------------------------------------------------
	// Can record commands now

	// if the handle passed is null, use the target for the current swapchain image
	const RenderTarget *target = render_pass->target.is_valid() ? 
		EV2_TYPE_PTR_CAST(RenderTarget, render_pass->target) : 
		frame->screen_target; 

	assert(target);

	VkRenderingAttachmentInfo color_info, depth_info, stencil_info; VkRenderingInfo rendering_info;

	populate_rendering_info(target, &rendering_info, &color_info, &depth_info, &stencil_info);

	vkCmdBeginRendering(cmds, &rendering_info);

	VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)render_pass->viewport.w;
    viewport.height = (float)render_pass->viewport.h;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmds, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = VkExtent2D{
		.width = render_pass->scissor.w,
		.height = render_pass->scissor.h
	};
    vkCmdSetScissor(cmds, 0, 1, &scissor);

	std::array<VkDescriptorSet,2> base_sets = {
		frame->descriptor_set,
		render_pass->descriptor_set,
	};

	VkPipelineLayout base_layout = ctx->get_base_pipeline_layout(EV2_BASE_SET_PER_PASS);

	vkCmdBindDescriptorSets(
		cmds, 
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		base_layout, 
		EV2_BASE_SET_PER_FRAME, 
		(uint32_t)base_sets.size(), base_sets.data(),
		0, nullptr
	);
}

static void rg_record_gfx_pass_end(RenderGraph*rg, const PassNode &node, VkCommandBuffer cmds)
{
	vkCmdEndRendering(cmds);
}

static VkResult rg_record_node(RenderGraph*rg, const PassNode &node, VkCommandBuffer cmds)
{
	if (node.commands.empty())
		return VK_SUCCESS;

	GfxContext *ctx = rg->ctx;

	uint32_t barrier_offset = 0;

	std::vector<VkImageMemoryBarrier2> image_barriers;
	std::vector<VkBufferMemoryBarrier2> buffer_barriers;

	const bool is_gfx_pass = node.is_gfx_node();

	if (!node.pre_barriers.empty()) {
		translate_barriers(
			ctx, 
			node.pre_barriers.data(),
			(uint32_t)node.pre_barriers.size(),
			buffer_barriers,
			image_barriers
		);
	}

	// TODO: This is fairly hacky.  Could move barriers into 'pre_barriers' instead.
	if (is_gfx_pass && !node.barriers.empty()) {
		const PassCommand &first_cmd = node.commands[0];
		translate_barriers(
			ctx, 
			&node.barriers[barrier_offset],
			first_cmd.barrier_count,
			buffer_barriers,
			image_barriers
		);
		barrier_offset += first_cmd.barrier_count;
	}

	if (!buffer_barriers.empty() || !image_barriers.empty()) {
		VkDependencyInfo dependency_info = 
			generate_dependency_info(ctx, buffer_barriers, image_barriers);

		vkCmdPipelineBarrier2(cmds, &dependency_info);
	}
	
	if (is_gfx_pass) {
		rg_record_gfx_pass_begin(rg, node, cmds);
	}

	for (const PassCommand &pass_cmd : node.commands) {

		// Without certain device features, barriers may not be recorded inside of
		// a render pass.
		if (!is_gfx_pass && pass_cmd.barrier_count) {
			image_barriers.clear();
			buffer_barriers.clear();

			translate_barriers(
				ctx, 
				&node.barriers[barrier_offset],
				pass_cmd.barrier_count,
				buffer_barriers,
				image_barriers
			);

			VkDependencyInfo dependency_info = 
				generate_dependency_info(ctx, buffer_barriers, image_barriers);

			vkCmdPipelineBarrier2(cmds, &dependency_info);

			barrier_offset += pass_cmd.barrier_count;
		}

		switch(pass_cmd.base.type) {
			case BindComputePipeline: {
				CmdBindComputePipeline cmd = pass_cmd.base.bind_compute_pipeline;
				ComputePipeline *pipeline = ctx->get_compute_pipeline(cmd.pipeline);

				vkCmdBindPipeline(cmds, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->base.pipeline);
				break;
			}
			case BindGfxPipeline: {
				CmdBindGfxPipeline cmd = pass_cmd.base.bind_gfx_pipeline;
				GfxPipeline *pipeline = ctx->get_gfx_pipeline(cmd.pipeline);

				vkCmdBindPipeline(cmds, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->base.pipeline);
				break;
			}
			case BindResources: {
				CmdBindResources cmd = pass_cmd.base.bind_resources;
				const Bindings *bindings = ctx->get_bindings(cmd.bindings);
				vkCmdBindDescriptorSets(
					cmds, 
					bindings->bind_point, 
					bindings->pipeline_layout,
					bindings->index, 
					1, &bindings->descriptor_set, 
					0, nullptr
				); 
				break;
			}
			case BindIndexBuffer: {
				CmdBindIndexBuffer cmd = pass_cmd.base.bind_index_buffer;
				Buffer *buffer = ctx->get_buffer(cmd.buffer);
				vkCmdBindIndexBuffer(cmds, buffer->buffer, cmd.offset, VK_INDEX_TYPE_UINT32);
				break;
			}
			case BindVertexBuffer: {
				CmdBindVertexBuffer cmd = pass_cmd.base.bind_vertex_buffer;
				Buffer *buffer = ctx->get_buffer(cmd.buffer);
				vkCmdBindVertexBuffers(cmds, 0, 1, &buffer->buffer, &cmd.offset);
				break;
			}
			case BindIndirectBuffer: {
				break;
			}
			case PushConstant: {
				CmdPushConstant cmd = pass_cmd.base.push_constant;
				const char *bytes = &node.pass->push_constant_data[cmd.src_offset];

				const BasePipeline *pipeline = cmd.pipeline;

				const std::vector<VkPushConstantRange> &ranges = 
					pipeline->layout_map->push_constant_ranges;

				auto it = std::lower_bound(
					ranges.begin(), 
					ranges.end(),
					VkPushConstantRange{
						.offset = cmd.offset
					},
					[](const VkPushConstantRange &r1, const VkPushConstantRange &r2) -> bool
					{
						return r1.offset < r2.offset;
					});

				VkShaderStageFlags stage_mask = 0;

				for(; it != ranges.end() && it->offset == cmd.offset; ++it) {
					stage_mask |= it->stageFlags;
				}

				VkPushConstantsInfo info = {
					.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
					.layout = pipeline->layout,
					.stageFlags = stage_mask,
					.offset = cmd.offset,
					.pValues = (void*)bytes,
				};

				vkCmdPushConstants2(cmds, &info);
				break;
			}
			case Dispatch: {
				CmdDispatch cmd = pass_cmd.base.dispatch;
				vkCmdDispatch(cmds, cmd.counts[0], cmd.counts[1], cmd.counts[2]);
				break;
			}
			case Custom: {
				CmdCustom cmd = pass_cmd.base.custom;
				node.pass->custom_callbacks[cmd.callback_id](cmds);

				break;
			}
			case UseBuffer:
			case UseImage:
			default:
				break;
		}
	}

	if (is_gfx_pass) {
		rg_record_gfx_pass_end(rg, node, cmds);
	}

	buffer_barriers.clear();
	image_barriers.clear();

	// TODO: This is fairly hacky.  Could move barriers into 'post_barriers' instead.
	if (is_gfx_pass && node.barriers.size() > barrier_offset) {

		uint32_t barrier_count = (uint32_t)node.barriers.size() - barrier_offset;
		translate_barriers(
			ctx, 
			&node.barriers[barrier_offset],
			barrier_count,
			buffer_barriers,
			image_barriers
		);
	}

	if (!node.post_barriers.empty()) {
		translate_barriers(
			ctx, 
			node.post_barriers.data(),
			(uint32_t)node.post_barriers.size(),
			buffer_barriers,
			image_barriers
		);
	}

	if (!buffer_barriers.empty() || !image_barriers.empty()) {
		VkDependencyInfo dependency_info = 
			generate_dependency_info(ctx, buffer_barriers,image_barriers);
		vkCmdPipelineBarrier2(cmds, &dependency_info);
	}

	return VK_SUCCESS;
}

static VkResult rg_record(GfxContext *ctx, RenderGraph *rg)
{
	FrameContext *frame = ctx->get_current_frame();
	uint32_t submission_count = (uint32_t)rg->submissions.size();

	VkResult result = VK_SUCCESS;

	std::vector<VkCommandBuffer> cmds (submission_count);

	result = frame->commands[0].get_cmds(submission_count, cmds.data()); 

	if (result != VK_SUCCESS)
		return result;

	for (uint32_t i = 0; i < submission_count; ++i) {
		rg->submissions[i].cmds = cmds[i];
	}

	for (RenderGraphSubmission& submission : rg->submissions) {
		VkCommandBufferBeginInfo begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
			.pInheritanceInfo = nullptr,
		};

		result = vkBeginCommandBuffer(submission.cmds, &begin_info); 

		if (result != VK_SUCCESS)
			break;

		for (uint32_t i = 0; i < (uint32_t)submission.nodes.size(); ++i) {
			const PassNode *node = submission.nodes[i];
			result = rg_record_node(rg, *node, submission.cmds);
			if (result != VK_SUCCESS)
				break;
		}

		result = vkEndCommandBuffer(submission.cmds);

		if (result != VK_SUCCESS)
			break;

	}

	if (result != VK_SUCCESS) {
		log_error("Render graph command recording failed");
	}
	return result;

}

static VkResult rg_submit(GfxContext *ctx, const RenderGraph *rg)
{
	std::vector<VkCommandBufferSubmitInfo> cmd_infos (rg->submissions.size());

	for (size_t i = 0; i < cmd_infos.size(); ++i) {
		cmd_infos[i] = VkCommandBufferSubmitInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.commandBuffer = rg->submissions[i].cmds,
			.deviceMask = 0,
		};
	}

	// queue_index -> submit infos
	std::unordered_map<uint32_t, std::vector<VkSubmitInfo2>> submission_map;

	for (size_t i = 0; i <  rg->submissions.size(); ++i) {
		const RenderGraphSubmission &submission = rg->submissions[i];

		auto [it, exists] = submission_map.emplace(
			submission.queue_index,std::vector<VkSubmitInfo2>());

		std::vector<VkSubmitInfo2>& submissions = it->second;

		submissions.emplace_back(VkSubmitInfo2{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.flags = 0,

			.waitSemaphoreInfoCount = (uint32_t)submission.wait.size(),
			.pWaitSemaphoreInfos = submission.wait.data(),

			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = &cmd_infos[i],

			.signalSemaphoreInfoCount = (uint32_t)submission.signal.size(),
			.pSignalSemaphoreInfos = submission.signal.data()
		});
	}

	VkResult result = VK_SUCCESS;

	for (const auto& [queue_index, submit_infos] : submission_map) {
		if (!rg->queues[queue_index]) {
			log_error("queue %d is not initialized!");
			abort();
		}
		
		result = rg->queues[queue_index]->submit(
			(uint32_t)submit_infos.size(), 
			submit_infos.data(), 
			VK_NULL_HANDLE
		);

		if (result != VK_SUCCESS) {
			break;
		}
	}

	if (result != VK_SUCCESS) {
		log_error("Render graph submission failed");
	}

	FrameContext *frame = ctx->get_current_frame();

	VkResult present_result = VK_SUCCESS;

	VkPresentInfoKHR present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &frame->render_finished_semaphore,
		.swapchainCount = 1,
		.pSwapchains = &ctx->swap_chain.swapchain,
		.pImageIndices = &frame->image_index,
		.pResults = &present_result,
	};

	result = vkQueuePresentKHR(ctx->present_queue, &present_info);
	if (result != VK_SUCCESS || present_result != VK_SUCCESS) {
		log_error("Present submission failed");
		return result;
	}
	return result;
}

static RenderGraph *rg_create(GfxContext *ctx)
{
	FrameContext *frame = ctx->get_current_frame();

	frame->render_graph = std::make_unique<RenderGraph>();
	RenderGraph *rg = frame->render_graph.get();
	rg->ctx = ctx;

	uint32_t pass_count = (uint32_t)frame->passes.size();

	rg->nodes.resize(pass_count);
	for (uint32_t i = 0; i < pass_count; ++i) {
		const Pass *pass = &frame->passes[i]; 

		rg->nodes[i] = PassNode {
			.pass = pass,
			.queue_family_index = pass->queue_family 
		};
	}

	uint32_t queue_family_count = (uint32_t)ctx->queue_families.size(); 

	rg->queues.resize(queue_family_count);

	for (uint32_t i = 0; i < queue_family_count; ++i) {
		QueueFamily &queue_family = ctx->queue_families[i];
		rg->queues[i] = queue_family.queues ? 
			&queue_family.queues[0] : nullptr;
	}

	return rg;
}

ev2::Result end_frame(GfxContext *ctx)
{
	ev2::Result result = ev2::SUCCESS;

	FrameContext *frame = ctx->get_current_frame();
	RenderGraph *rg = rg_create(ctx);

	result = rg_compile(ctx, rg);
	if (result != ev2::SUCCESS) {
		return set_error(ev2::ERENDER_GRAPH,"Failed to compile render graph");
	}

	VkResult vk_result = VK_SUCCESS;

	vk_result = rg_record(ctx, rg);
	if (vk_result != VK_SUCCESS) {
		return set_error(ev2::ERENDER_GRAPH,"Failed to record render graph");
	}

	vk_result = rg_submit(ctx, rg);
	if (vk_result != VK_SUCCESS) {
		return set_error(ev2::ERENDER_GRAPH,"Failed to submit render graph");
	}

	// After recording the render graph, any semaphores not reused from the 
	// previous frame are destroyed 
	frame->cull_unused_syncs();

	++ctx->frame_counter;
	return ev2::SUCCESS;
}

};

