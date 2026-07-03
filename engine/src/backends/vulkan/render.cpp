#include <ev2/defines.h>

#include "backends/vulkan/context_impl.h"
#include "backends/vulkan/pipeline_impl.h"

#include <glm/gtc/type_ptr.hpp>

#include <cstring>

#define DEFAULT_DEVICE_INDEX 0

constexpr uint32_t INDEX_NONE = (uint32_t)-1;

namespace ev2 {

static Result reset_frame_context(GfxContext *ctx, FrameContext *frame)
{
	Result result = ev2::SUCCESS;

	VkAcquireNextImageInfoKHR acquire_info = {
		.sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
		.swapchain = ctx->swap_chain.swapchain,
		.timeout = 0,
		.semaphore = frame->image_available_sempahore,
		.fence = VK_NULL_HANDLE,
		.deviceMask = 0,
	};

	uint32_t image_index;
    VkResult vk_result = vkAcquireNextImage2KHR(ctx->device, &acquire_info, &image_index);

	if (vk_result == VK_ERROR_OUT_OF_DATE_KHR) {
        result = ctx->reset_swap_chain();
    } else if (vk_result != VK_SUCCESS && vk_result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

	frame->swap_image_use_count = 0;

	if (result != SUCCESS)
		return result;

	frame->screen_target = ctx->swap_chain_targets[image_index];
	frame->swap_chain_image_index = image_index;

	// Skip cleanup if not used yet
	
	if (frame->index < ctx->max_frames_in_flight)
		return SUCCESS;

	frame->render_graph.reset();

	//------------------------------------------------------------------------------
	// cleaup previous state

	for (const VkCommandPool &command_pool : frame->command_pools) {
		vk_result = vkResetCommandPool(ctx->device, command_pool, 0);

		if (result)
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

	VkImageViewCreateInfo view_info {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.flags = 0, 
		.image = vk_image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_D32_SFLOAT_S8_UINT,
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
		.base_view = VK_NULL_HANDLE,
		.aspect_mask = aspect_mask,
		.w = w,
		.h = h,
		.d = 1,
		.levels = 1,
		.format = IMAGE_FORMAT_32F
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

	if (result != VK_SUCCESS)
		return result;

	VkImageViewCreateInfo view_info {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.flags = 0, 
		.image = vk_image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_R32_SFLOAT,
		.components = {},
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
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
		.base_view = VK_NULL_HANDLE,
		.aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT,
		.w = w,
		.h = h,
		.d = 1,
		.levels = 1,
		.format = IMAGE_FORMAT_32F
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
	static float rgb[3] = {0.0f,0.0f,0.0f};

	bool has_color = p_target->color_view != VK_NULL_HANDLE;
	bool has_depth = p_target->depth_view != VK_NULL_HANDLE;
	bool has_stencil = has_depth;

	if (has_color) {
		*p_color_info = VkRenderingAttachmentInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = p_target->color_view,
			.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.resolveMode = VK_RESOLVE_MODE_NONE,
			.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
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
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			.resolveMode = VK_RESOLVE_MODE_NONE,
			.resolveImageView = VK_NULL_HANDLE,
			.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue = VkClearValue{
				.depthStencil = {
					.depth = 0,
					.stencil = 0
				}
			},
		};
	}
	if (has_stencil) {
		*p_stencil_info = VkRenderingAttachmentInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = p_target->depth_view,
			.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
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
	if (EV2_IS_NULL(ctx->view_data.buffer))
		log_error("No views created.  Did you remember to call begin_frame()?"); 

	if (view_handle.id == 0) {
		view_handle = ctx->default_view;
	}

	Pass *pass = new Pass{
		.ctx = ctx,
		.queue_family = ctx->graphics_family->index
	};

	const FrameContext *frame = ctx->get_current_frame();

	std::unique_ptr<RenderPass> gfx (new RenderPass{
		.target = target_handle,
		.view = view_handle,
		.viewport = in_viewport,
		.scissor = in_scissor
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

	pass->gfx = gfx.release();

	const bool render_to_swapchain = target_handle.is_valid();

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
					.usage = USAGE_DEPTH_ATTACHMENT, 
				},
				.type = UseImage
			});		
	}

	return EV2_HANDLE_CAST(Pass, pass); 
}

PassID begin_compute_pass(GfxContext *ctx)
{
	Pass *pass = new Pass{
		.ctx = ctx,
		.queue_family = ctx->graphics_family->index
	};

	return EV2_HANDLE_CAST(Pass, pass); 

}

void end_pass(GfxContext *ctx, PassID pass_handle)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_handle);

	FrameContext *frame = ctx->get_current_frame();
	frame->add_pass(pass);
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
				.access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 
				.stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT_KHR |
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

void cmd_bind_resources(PassID pass_id, ShaderBindingsID bindings_id)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_id);

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

Result begin_frame(GfxContext *ctx)
{
	if (!ctx->is_swap_chain_valid)
		ctx->reset_swap_chain();

	FrameContext *frame = ctx->get_current_frame();

	//------------------------------------------------------------------------------
	// Wait until frame at this index is complete before resetting it.
	Result result = ctx->wait_for_frame_completion(frame);

	if (result != SUCCESS)
		return result;

	frame->index = ctx->frame_counter++;

	//------------------------------------------------------------------------------
	// Cleanup frame context if previously used
	
	result = reset_frame_context(ctx, frame);

	if (result != SUCCESS)
		return result;

	//------------------------------------------------------------------------------
	// Update stuff

	uint64_t time_ns = 
		std::chrono::high_resolution_clock::now().time_since_epoch().count();

	double time_seconds = (double)(time_ns - ctx->start_time_ns)/1e9;

	frame->dt = time_seconds - ctx->get_previous_frame()->t;
	frame->t = time_seconds;

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
	uint64_t sync = commit_buffer_uploads(ctx, uc, frame->ubo, &up, 1);
	flush_uploads(ctx);
	wait_complete(ctx, sync);

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

static void update_pass_barriers(RenderGraph &rg, const PassBarrier& barrier,
								 uint32_t src_pass_idx, uint32_t dst_pass_idx)
{
	const TaggedResource resource = barrier.resource;
	const bool is_write_barrier = barrier.is_write;

	PassNode *dst_node = &rg.nodes[dst_pass_idx];

	bool needs_queue_transfer = 
		barrier.dst_state.queue_family_index != barrier.src_state.queue_family_index; 

	// TODO: Handle situation where resource had been used by another queue family 
	// not in this frame.

	if (needs_queue_transfer && src_pass_idx != PASS_INDEX_OUT_OF_FRAME) {
		rg.nodes[src_pass_idx].barriers.push_back(barrier);
	}

	PassEdge *edge = get_or_insert_edge(&rg, PassEdge{
		.src_pass = src_pass_idx,
		.dst_pass = dst_pass_idx,
		.barrier = barrier
	});

	// When there was previously a read barrier on the resource, and we're
	// doing another read, we will union the destination flags of the existing
	// barrier with the new destination flags instead of adding a redundant
	// barrier.

	PassBarrier *final_barrier;
	if (
		final_barrier = dst_node->barriers.empty() ? nullptr : &dst_node->barriers.back();

		!is_write_barrier && final_barrier && 
		!final_barrier->is_write && final_barrier->resource == resource
	) {
		final_barrier->dst_state.access |= barrier.dst_state.access;
		final_barrier->dst_state.stage |= barrier.dst_state.stage;

		// update the barrier for the edge
		edge->barrier = *final_barrier;
	} else {
		dst_node->barriers.push_back(barrier);
		final_barrier = &dst_node->barriers.back();
	}

	dst_node->final_states[resource] = final_barrier->dst_state;
}

void rg_use_buffer(RenderGraph &rg, GfxContext *ctx, uint32_t dst_node_idx, const CmdUseBuffer& cmd)
{
	auto [it, inserted] = rg.buffer_passes.emplace(cmd.buffer.id, dst_node_idx);

	const PassNode &dst_node = rg.nodes[dst_node_idx];

	Buffer *buf = ctx->get_buffer(cmd.buffer);
	ResourceState *p_state = &buf->state;

	ResourceStateFlags src_flags = update_resource_state(p_state, cmd.usage, 
													  dst_node.queue_family_index);
	ResourceStateFlags dst_flags = p_state->get_current();

	PassBarrier barrier = {
		.src_state = src_flags,
		.dst_state = dst_flags,
		.resource = TaggedResource(cmd.buffer),
		.is_write = (bool)(usage_to_rw_flags(cmd.usage) & PASS_WRITE)
	};

	uint32_t src_pass_idx = inserted ? it->second : PASS_INDEX_OUT_OF_FRAME;
	update_pass_barriers(rg, barrier, src_pass_idx, dst_node_idx);

	if (!inserted)
		it->second = dst_node_idx;
}

void rg_use_image(RenderGraph &rg, GfxContext *ctx, uint32_t dst_node_idx, const CmdUseImage& cmd)
{
	auto [it, inserted] = rg.image_passes.emplace(cmd.image.id, dst_node_idx);

	const PassNode &dst_node = rg.nodes[dst_node_idx];

	Image *img = ctx->get_image(cmd.image);
	ResourceState *p_state = &img->state;

	ResourceStateFlags src_flags = update_resource_state(p_state, cmd.usage, 
													  dst_node.queue_family_index);
	ResourceStateFlags dst_flags = p_state->get_current();

	PassBarrier barrier = {
		.src_state = src_flags,
		.dst_state = dst_flags,
		.resource = TaggedResource(cmd.image),
		.is_write = (bool)(usage_to_rw_flags(cmd.usage) & PASS_WRITE)
	};

	uint32_t src_pass_idx = inserted ? it->second : PASS_INDEX_OUT_OF_FRAME;
	update_pass_barriers(rg, barrier, src_pass_idx, dst_node_idx);

	if (!inserted)
		it->second = dst_node_idx;
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
	
	uint32_t last_screen_render_index = INDEX_NONE;

	for (const PassNode &node : rg->nodes) {
		uint32_t queue_family_index = node.pass->queue_family;

		RenderGraphSubmission &submission = rg->submissions.emplace_back(RenderGraphSubmission{
			.nodes = {&node},
			.queue_index = queue_family_index
		});

		uint32_t submission_index = (uint32_t)rg->submissions.size() - 1;

		// If we render to the swapchain image, 
		if (node.pass->gfx && node.pass->gfx->render_to_swapchain()) {
			if (!frame->swap_image_use_count++) {
				submission.wait.push_back(VkSemaphoreSubmitInfo{
					.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
					.semaphore = frame->image_available_sempahore,
					.stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					.deviceIndex = DEFAULT_DEVICE_INDEX,
				});
			}
			last_screen_render_index = submission_index;
		}
	}

	if (last_screen_render_index != INDEX_NONE) {
		rg->submissions[last_screen_render_index].signal.push_back(VkSemaphoreSubmitInfo{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = frame->render_finished_semaphore,
			.stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.deviceIndex = DEFAULT_DEVICE_INDEX,
		});
		rg->submissions[last_screen_render_index].signal.push_back(VkSemaphoreSubmitInfo{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = ctx->frame_semaphore,
			.value = frame->index,
			.stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.deviceIndex = DEFAULT_DEVICE_INDEX,
		});
	} else {
		log_error("Incomplete render graph: does not render to display");
		return ev2::ERENDER_GRAPH;
	}

	//------------------------------------------------------------------------------
	// Populate semaphores
	//
	// After assigning each node in the graph to a submission group (i.e.,
	// a group of nodes, specific queue, and wait + signal semaphores), 
	//
	// Populate the semaphores for that submission group based on a 

	for (const PassEdge &edge : rg->edges) {
		if (edge.is_on_same_queue())
			continue;

		PassNode &dst_node = rg->nodes[edge.dst_pass];
		RenderGraphSubmission &submission = rg->submissions[dst_node.submission_idx];

		ResourceSync *done_sync = 
			done_sync = frame->get_resource_sync(
				edge.barrier.resource, 
				rg->queues[submission.queue_index]->queue
			);

		assert(done_sync);

		++done_sync->wait_value;

		ResourceState *state = nullptr;
		switch (edge.barrier.resource.type) {
			case RESOURCE_TYPE_BUFFER: {
				Buffer *buffer = ctx->get_buffer(edge.barrier.resource.to_buffer());
				state = &buffer->state;
				break;
			}
			case RESOURCE_TYPE_IMAGE: {
				Image *image = ctx->get_image(edge.barrier.resource.to_image());
				state = &image->state;
				break;
			}
		}
		assert(state);

		uint32_t wait_sync_count;
		const ResourceSync *wait_syncs;

		state->get_current_sync(&wait_sync_count, &wait_syncs);

		for (uint32_t i = 0; i < wait_sync_count; ++i) {
			submission.wait.push_back(VkSemaphoreSubmitInfo{
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				.semaphore = wait_syncs[i].semaphore,
				.value = wait_syncs[i].wait_value,
				.stageMask = edge.barrier.dst_state.stage,
				.deviceIndex = DEFAULT_DEVICE_INDEX,
			});
		}
		submission.signal.push_back(VkSemaphoreSubmitInfo{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = done_sync->semaphore,
			.value = done_sync->wait_value,
			.stageMask = dst_node.final_states[edge.barrier.resource].stage,
			.deviceIndex = DEFAULT_DEVICE_INDEX,
		});

		if (edge.barrier.is_write) {
			state->sync_write(done_sync->semaphore, done_sync->wait_value);
		} else {
			state->sync_read(done_sync->semaphore, done_sync->wait_value);
		}
	}

	return ev2::SUCCESS;
}

static VkDependencyInfo generate_dependency_info(
	GfxContext *ctx,
	const PassBarrier *barriers,
	uint32_t count,
	std::vector<VkBufferMemoryBarrier2>& buffer_barriers,
	std::vector<VkImageMemoryBarrier2>& image_barriers
)
{
	for (const PassBarrier *p_barrier = barriers; p_barrier < barriers + count; ++p_barrier) {
		const PassBarrier &barrier = *p_barrier;

		bool is_queue_transfer = 
			barrier.src_state.queue_family_index != barrier.dst_state.queue_family_index;

		switch (barrier.resource.type) {
			case RESOURCE_TYPE_BUFFER: {
				const Buffer *buffer = ctx->get_buffer(barrier.resource.to_buffer());
				buffer_barriers.push_back(VkBufferMemoryBarrier2{
					.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,

					.srcStageMask = barrier.src_state.stage,
					.srcAccessMask = barrier.src_state.access,

					.dstStageMask = barrier.dst_state.stage,
					.dstAccessMask = barrier.dst_state.access,

					.srcQueueFamilyIndex = is_queue_transfer ?
						barrier.src_state.queue_family_index : VK_QUEUE_FAMILY_IGNORED,
					.dstQueueFamilyIndex = is_queue_transfer ?
						barrier.dst_state.queue_family_index : VK_QUEUE_FAMILY_IGNORED,

					.buffer = buffer->buffer,
					.offset = 0,
					.size = buffer->size
				});

				break; 
			}
			case RESOURCE_TYPE_IMAGE: {
				const Image *image = ctx->get_image(barrier.resource.to_image());
				image_barriers.push_back(VkImageMemoryBarrier2{
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,

					.srcStageMask = barrier.src_state.stage,
					.srcAccessMask = barrier.src_state.access,

					.dstStageMask = barrier.dst_state.stage,
					.dstAccessMask = barrier.dst_state.access,

					.oldLayout = barrier.src_state.layout,
					.newLayout = VK_IMAGE_LAYOUT_GENERAL,

					.srcQueueFamilyIndex = is_queue_transfer ?
						barrier.src_state.queue_family_index : VK_QUEUE_FAMILY_IGNORED,
					.dstQueueFamilyIndex = is_queue_transfer ?
						barrier.dst_state.queue_family_index : VK_QUEUE_FAMILY_IGNORED,

					.image = image->image,
					.subresourceRange = VkImageSubresourceRange{
						.aspectMask = image->aspect_mask,
						.baseMipLevel = 0,
						.levelCount = image->levels,
						.baseArrayLayer = 0,
						.layerCount = image->d,
					}
				});
				break;
			}
		}
	}
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
	const RenderPass *render_pass = node.pass->gfx;

	assert(render_pass);

	//------------------------------------------------------------------------------
	// Can record commands now

	// if the handle passed is null, use the target for the current swapchain image
	const RenderTarget *target = render_pass->target.is_valid() ? 
		EV2_TYPE_PTR_CAST(RenderTarget, render_pass->target) : 
		frame->screen_target; 

	VkRenderingAttachmentInfo color_info, depth_info, stencil_info;
	VkRenderingInfo rendering_info;

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

	std::array<VkDescriptorSet,3> base_sets = {
		VK_NULL_HANDLE,
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
	GfxContext *ctx = rg->ctx;

	uint32_t barrier_offset = 0;

	std::vector<VkImageMemoryBarrier2> image_barriers;
	std::vector<VkBufferMemoryBarrier2> buffer_barriers;

	const bool is_gfx_pass = node.is_gfx_node();

	if (is_gfx_pass) {
		rg_record_gfx_pass_begin(rg, node, cmds);
	}

	for (const PassCommand &pass_cmd : node.commands) {
		if (pass_cmd.barrier_count) {
			image_barriers.clear();
			buffer_barriers.clear();

			VkDependencyInfo dependency_info = generate_dependency_info(
				ctx, 
				&node.barriers[barrier_offset],
				pass_cmd.barrier_count,
				buffer_barriers,
				image_barriers
			);

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
				const ShaderBindings *bindings = ctx->get_shader_bindings(cmd.bindings);
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
				break;
			}
			case BindVertexBuffer: {
				break;
			}
			case BindIndirectBuffer: {
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

	return VK_SUCCESS;
}

static VkResult rg_record(GfxContext *ctx, RenderGraph *rg)
{
	VkResult result = VK_SUCCESS;

	std::vector<VkCommandBuffer> cmds (rg->submissions.size());

	FrameContext *frame = ctx->get_current_frame();

	VkCommandBufferAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = frame->command_pools[0],
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = (uint32_t)cmds.size(),
	};

	result = vkAllocateCommandBuffers(ctx->device, &alloc_info, cmds.data()); 

	if (result != VK_SUCCESS)
		return result;

	for (RenderGraphSubmission& submission : rg->submissions) {

		VkCommandBufferBeginInfo begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
			.pInheritanceInfo = nullptr,
		};

		result = vkBeginCommandBuffer(submission.cmds, &begin_info); 

		if (result != VK_SUCCESS)
			break;

		for (const PassNode *node : submission.nodes) {
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
		.pImageIndices = &frame->swap_chain_image_index,
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

	uint32_t pass_count = (uint32_t)frame->passes.size();

	rg->nodes.resize(pass_count);
	for (uint32_t i = 0; i < pass_count; ++i) {
		const Pass *pass = frame->passes[i]; 

		rg->nodes[i] = PassNode {
			.pass = pass,
			.queue_family_index = pass->queue_family 
		};
	}

	uint32_t queue_family_count = (uint32_t)ctx->queue_families.size(); 

	for (uint32_t i = 0; i < queue_family_count; ++i) {
		QueueFamily &queue_family = ctx->queue_families[i];
		rg->queues[i] = queue_family.queues ? 
			&queue_family.queues[0] : nullptr;
	}

	return rg;
}

void end_frame(GfxContext *ctx)
{
	ev2::Result result = ev2::SUCCESS;

	RenderGraph *rg = rg_create(ctx);

	result = rg_compile(ctx, rg);
	if (result != ev2::SUCCESS) {
		return;
	}

	VkResult vk_result = VK_SUCCESS;

	vk_result = rg_record(ctx, rg);
	if (vk_result != VK_SUCCESS) {
		return;
	}

	vk_result = rg_submit(ctx, rg);
	if (vk_result != VK_SUCCESS) {
		return;
	}
}

};

