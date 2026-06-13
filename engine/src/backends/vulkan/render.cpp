#include <ev2/defines.h>

#include "backends/vulkan/context_impl.h"
#include "backends/vulkan/pipeline_impl.h"

#include <glm/gtc/type_ptr.hpp>

#include <cstring>

static ev2::Result reset_frame_context(ev2::GfxContext *ctx, FrameContext *frame)
{
	VkAcquireNextImageInfoKHR acquire_info = {
		.sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
		.swapchain = ctx->swap_chain.swapchain,
		.timeout = 0,
		.semaphore = frame->image_available_sempahore,
		.deviceMask = 0,
		.fence = VK_NULL_HANDLE,
	};

	uint32_t image_index;
    VkResult vk_result = vkAcquireNextImage2KHR(ctx->device, &acquire_info, &image_index);

	ev2::Result result = ev2::SUCCESS;
	
	if (vk_result == VK_ERROR_OUT_OF_DATE_KHR) {
        result = ctx->reset_swap_chain();
    } else if (vk_result != VK_SUCCESS && vk_result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

	if (result != ev2::SUCCESS)
		return result;

	frame->screen_target = ctx->swap_chain_targets[image_index];

	// Skip cleanup if not used yet
	
	if (ctx->current_frame_index < ctx->max_frames_in_flight)
		return ev2::SUCCESS;

	//------------------------------------------------------------------------------
	// cleaup previous state

	for (const VkCommandPool &command_pool : frame->command_pools) {
		vk_result = vkResetCommandPool(ctx->device, command_pool, 0);

		if (result)
			return ev2::EUNKNOWN;
	}

	vk_result = vkResetDescriptorPool(ctx->device, frame->descriptor_pool, 0);

	if (vk_result != VK_SUCCESS)
		return ev2::EUNKNOWN;

	return ev2::SUCCESS;
}

static VkResult create_depth_image(ev2::GfxContext *ctx, uint32_t w, uint32_t h,
									  ev2::ImageID* out_image, VkImageView *out_view)
{
	VkImageCreateInfo create_info  = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    	.flags = 0,
    	.imageType = VK_IMAGE_TYPE_2D, 
    	.format = VK_FORMAT_R32_SFLOAT,
    	.extent = VkExtent3D{
			.width = w,
			.height = h,
			.depth = 1,
		},
    	.mipLevels = 0, 
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

	ev2::Image image = {
		.image = vk_image,
		.allocation = allocation,
		.base_view = VK_NULL_HANDLE,
		.layout = VK_IMAGE_LAYOUT_UNDEFINED,
		.aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT,
		.w = w,
		.h = h,
		.d = 1,
		.levels = 1,
		.format = ev2::IMAGE_FORMAT_32F
	};

	ev2::ImageID handle = ctx->emplace_image(std::move(image));

	*out_image = handle;
	*out_view = view;

	return result;
}

static VkResult create_color_image(ev2::GfxContext *ctx, uint32_t w, uint32_t h,
									  ev2::ImageID* out_image, VkImageView *out_view)
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
    	.mipLevels = 0, 
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

	ev2::Image image = {
		.image = vk_image,
		.allocation = allocation,
		.base_view = VK_NULL_HANDLE,
		.layout = VK_IMAGE_LAYOUT_UNDEFINED,
		.aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT,
		.w = w,
		.h = h,
		.d = 1,
		.levels = 1,
		.format = ev2::IMAGE_FORMAT_32F
	};

	ev2::ImageID handle = ctx->emplace_image(std::move(image));

	*out_image = handle;
	*out_view = view;

	return result;
}

//------------------------------------------------------------------------------
// Interface

namespace ev2 {

RenderTarget *create_render_target_internal(
	GfxContext *ctx,
	uint32_t w, uint32_t h,
	VkImageView color_view,
	VkImageView depth_view,
	RenderTargetFlags flags
)
{
	if ((flags & RENDER_TARGET_CREATE_COLOR_BIT))
		return nullptr;

	RenderTarget *target = new RenderTarget{};

	target->w = w;
	target->h = h;
	target->flags = flags;

	VkResult result = VK_SUCCESS; 

	if (flags & RENDER_TARGET_CREATE_DEPTH_BIT) {
		result = create_depth_image(ctx, w, h, &target->depth_img, &target->depth_view);
		if (result)
			goto cleanup;
	}

	if (flags & RENDER_TARGET_CREATE_COLOR_BIT) {
		result = create_color_image(ctx, w, h, &target->color_img, &target->color_view);
		if (result)
			goto cleanup;
	}

	return nullptr;
cleanup:
	destroy_render_target_internal(ctx, target);
	return {};
}

void destroy_render_target_internal(
	GfxContext *ctx, 
	RenderTarget* target
)
{
	if (target->flags & RENDER_TARGET_CREATE_DEPTH_BIT) {
		ev2::destroy_image(ctx, target->depth_img);
		vkDestroyImageView(ctx->device, target->depth_view, nullptr);
	}

	if (target->flags & RENDER_TARGET_CREATE_COLOR_BIT) {
		ev2::destroy_image(ctx, target->color_img);
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


static RenderPass *create_gfx_pass(
	GfxContext *ctx, 
	RenderTargetID target_handle,
	ViewID view_handle,
	Rect in_viewport,
	Rect in_scissor
)
{
	const FrameContext *frame = ctx->get_current_frame();

	std::unique_ptr<RenderPass> pass (new RenderPass{
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
		vkAllocateDescriptorSets(ctx->device, &alloc_info, &pass->descriptor_set);

	if (result != VK_SUCCESS)
		return nullptr;

	Buffer * buf = ctx->get_buffer(ctx->view_data.buffer);

	VkDescriptorBufferInfo buffer_info = {
		.buffer = buf->buffer,
		.offset = ctx->view_data.get_offset((uint32_t)view_handle.id),
		.range = ctx->view_data.stride
	};

	VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = pass->descriptor_set,
		.dstBinding = 0,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &buffer_info
	};

	vkUpdateDescriptorSets(ctx->device, 1, &write, 0, nullptr);

	return pass.release();
}

static VkCommandBuffer create_pass_commands(GfxContext *ctx)
{
	VkCommandBufferAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = ctx->get_current_frame_command_pool(),  
		.commandBufferCount = 1,
	};
	
	VkCommandBuffer command_buffer;

	VkResult result = 
		vkAllocateCommandBuffers(ctx->device, &alloc_info, &command_buffer);
	if (result != VK_SUCCESS)
		return nullptr;

	if (result != VK_SUCCESS)
		return nullptr;
	return command_buffer;
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
		.pStencilAttachment = nullptr,
	};
}

static void record_base_gfx_pass_cmds(const Pass *pass, VkCommandBuffer cmds)
{
	const FrameContext *frame = pass->ctx->get_current_frame(); 

	VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)pass->gfx->viewport.w;
    viewport.height = (float)pass->gfx->viewport.h;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmds, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = VkExtent2D{
		.width = pass->gfx->scissor.w,
		.height = pass->gfx->scissor.h
	};
    vkCmdSetScissor(cmds, 0, 1, &scissor);

	std::array<VkDescriptorSet,2> base_sets = {
		frame->descriptor_set,
		pass->gfx->descriptor_set,
	};

	VkPipelineLayout base_layout = pass->ctx->get_base_pipeline_layout(EV2_BASE_SET_PER_PASS);

	vkCmdBindDescriptorSets(
		cmds, 
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		base_layout, 
		EV2_BASE_SET_PER_FRAME, 
		(uint32_t)base_sets.size(), base_sets.data(),
		0, nullptr
	);
}

PassID begin_pass(
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
	};
	pass->sync = {
		.pass = pass
	};

	RenderPass *gfx = create_gfx_pass(
		ctx, 
		target_handle, 
		view_handle,
		in_viewport,
		in_scissor
	);

	if (!gfx)
		return EV2_NULL_HANDLE(Pass);

	pass->gfx = gfx;


	VkCommandBuffer cmds = create_pass_commands(ctx); 
	if (!cmds)
		return EV2_NULL_HANDLE(Pass);

	VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr,
	};

	VkResult result = vkBeginCommandBuffer(cmds, &begin_info);

	if (result)
		return EV2_NULL_HANDLE(Pass);

	const FrameContext *frame = ctx->get_current_frame(); 

	//------------------------------------------------------------------------------
	// Can record commands now

	// if the handle passed in null, use the target for the current swapchain image
	const RenderTarget *target = target_handle.is_valid() ? 
		EV2_TYPE_PTR_CAST(RenderTarget, target_handle) : 
		frame->screen_target; 

	VkRenderingAttachmentInfo color_info, depth_info, stencil_info;
	VkRenderingInfo rendering_info;

	populate_rendering_info(target, &rendering_info, &color_info, &depth_info, &stencil_info);

	vkCmdBeginRendering(cmds, &rendering_info);

	record_base_gfx_pass_cmds(pass, cmds);

	return EV2_HANDLE_CAST(Pass, pass); 
}

SyncID end_pass(GfxContext *ctx, PassID pass_handle)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_handle);

	FrameContext *frame = ctx->get_current_frame();
	frame->add_pass(pass);

	//vkCmdEndRendering(pass->cmds);
	//VkResult result = vkEndCommandBuffer(pass->cmds);

	//if (result != VK_SUCCESS)
	//	return EV2_NULL_HANDLE(Sync);

	return EV2_HANDLE_CAST(Sync, &pass->sync);
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

static uint8_t usage_to_rw_flags(ev2::Usage usage)
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

	//switch(usage) {
	//	case USAGE_TRANSFER_SRC:
	//		return state->set_read(flags.access, flags.stage);
	//	case USAGE_TRANSFER_DST:
	//		return state->set_write(flags.access, flags.stage);
	//	case USAGE_SAMPLED_GRAPHICS:
	//		return state->set_read(flags.access, flags.stage);
	//	case USAGE_COLOR_ATTACHMENT:
	//		return state->set_write(flags.access, flags.stage);
	//	case USAGE_DEPTH_ATTACHMENT:
	//		return state->set_write(flags.access, flags.stage);
	//	case USAGE_STORAGE_READ_COMPUTE:
	//		return state->set_read(flags.access, flags.stage);
	//	case USAGE_STORAGE_RW_COMPUTE:
	//		return state->set_write(flags.access, flags.stage);
	//	case USAGE_MAX_ENUM:
	//	case USAGE_UNDEFINED:
	//	default:
	//		return {};
	//}
	//return {};
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
		.type = ev2::UseBuffer,
	});

	//return;

	//Buffer *buf = pass->ctx->get_buffer(buf_id);

	//ResourceStateFlags old_flags = update_resource_state(&buf->state, usage);
	//ResourceStateFlags curr_flags = buf->state.get_current();

	//VkBufferMemoryBarrier2 barrier = {
	//	.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
	//	.srcStageMask = old_flags.stage,
	//	.srcAccessMask = old_flags.access,
	//	.dstStageMask = curr_flags.stage,
	//	.dstAccessMask = curr_flags.access,
	//	.buffer = buf->buffer,
	//	.offset = 0,
	//	.size = buf->size,
	//	.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	//	.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	//};
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
		.type = ev2::UseImage,
	});
	//return;

	//Image *img = pass->ctx->get_image(img_id);

	//ResourceStateFlags old_flags = update_resource_state(&img->state, usage);
	//ResourceStateFlags curr_flags = img->state.get_current();

	//VkImageMemoryBarrier2 barrier = {
	//	.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
	//	.srcStageMask = old_flags.stage,
	//	.srcAccessMask = old_flags.access,
	//	.dstStageMask = curr_flags.stage,
	//	.dstAccessMask = curr_flags.access,
	//	.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
	//	.newLayout = VK_IMAGE_LAYOUT_GENERAL,
	//	.image = img->image,
	//	.subresourceRange = VkImageSubresourceRange{
	//		.aspectMask = img->aspect_mask,
	//		.baseMipLevel = 0,
	//		.levelCount = 1,
	//		.baseArrayLayer = 0,
	//		.layerCount = 1,
	//	},
	//	.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	//	.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	//};
}

void cmd_bind_resources(PassID pass_id, ShaderBindingsID bindings_id)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_id);

	pass->cmds.push_back(Command{
		.bind_resources = {
			.bindings = bindings_id
		},
		.type = ev2::BindResources,
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
		.type = ev2::BindComputePipeline,
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
		.type = ev2::Dispatch,
	});
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
		.type = ev2::BindGfxPipeline,
	});
}

void cmd_bind_index_buffer(PassID pass_id, BufferID buf_id)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_id);

	Buffer *buf = pass->ctx->get_buffer(buf_id);
}

void cmd_draw_screen_quad(PassID pass_id)
{
	Pass *pass = EV2_TYPE_PTR_CAST(Pass, pass_id);
}

//------------------------------------------------------------------------------
// Render graph compilation

ev2::Result begin_frame(GfxContext *ctx)
{
	if (!ctx->is_swap_chain_valid)
		ctx->reset_swap_chain();

	FrameContext *frame = ctx->get_current_frame();
	++ctx->current_frame_index;

	//------------------------------------------------------------------------------
	// Wait until commands on oldest frame have completing before resetting it.
	ev2::Result result = ctx->wait_for_frame_completion(frame);

	if (result != ev2::SUCCESS)
		return result;

	//------------------------------------------------------------------------------
	// Cleanup frame context if previously used
	
	result = reset_frame_context(ctx, frame);

	if (result != ev2::SUCCESS)
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
	ev2::flush_uploads(ctx);
	ev2::wait_complete(ctx, sync);

	return ev2::SUCCESS;
}

enum ResourceType : uint8_t {
	PASS_EDGE_BUFFER,
	PASS_EDGE_IMAGE
};

struct PassBarrier 
{
	union {
		ImageID image;
		BufferID buffer;
	};
	ResourceStateFlags src_state;
	ResourceStateFlags dst_state;
	ResourceType type;
};

struct PassEdge
{
	uint32_t src_pass;
	uint32_t dst_pass;

	uint64_t use_count;

	PassBarrier barrier;
};

struct PassCommand
{
	ev2::Command base;
	uint32_t barrier_count;
};

struct PassSync
{
	VkSemaphore semaphore;
	uint64_t wait_value;
};

struct PassParse
{
	std::vector<PassCommand> commands;
	std::vector<PassBarrier> barriers;

	std::vector<PassSync> wait;
	std::vector<PassSync> signal;

	uint32_t barrier_offset;
};

struct RenderGraph
{
	GfxContext *ctx;

	const ev2::Pass *passes;

	std::vector<VkCommandBuffer> command_buffers;
	std::vector<PassParse> parses;
	std::vector<PassEdge> edges;

	std::unordered_map<uint64_t, uint32_t> buffer_passes;
	std::unordered_map<uint64_t, uint32_t> image_passes;
};

static void rg_use_buffer(RenderGraph &rg, GfxContext *ctx, uint32_t pass_idx, const CmdUseBuffer &cmd);
static void rg_use_image(RenderGraph &rg, GfxContext *ctx, uint32_t pass_idx, const CmdUseImage &cmd);

static void update_pass_barriers(RenderGraph &rg, const PassBarrier& barrier,
								 uint32_t src_pass_idx, uint32_t dst_pass_idx)
{
	if (src_pass_idx != dst_pass_idx) {
		PassEdge edge = {
			.src_pass = src_pass_idx,
			.dst_pass = dst_pass_idx,
			.barrier = barrier,
		};
		rg.edges.push_back(edge);
	}

	PassParse *src_parse = &rg.parses[src_pass_idx];
	PassParse *dst_parse = &rg.parses[dst_pass_idx];

	// TODO: Handle situation where resource had been used by another queue family 
	// not in this frame.

	if (barrier.dst_state.queue_family_index != barrier.src_state.queue_family_index) {
		PassBarrier src_barrier = barrier;
		src_barrier.dst_state.stage = 0;
		src_barrier.dst_state.access = 0;

		PassBarrier dst_barrier = barrier;
		dst_barrier.src_state.stage = 0;
		dst_barrier.src_state.access = 0;

		src_parse->barriers.push_back(src_barrier);
		dst_parse->barriers.push_back(dst_barrier);
	} else {
		src_parse->barriers.push_back(barrier);
	}
}

void rg_use_buffer(RenderGraph &rg, GfxContext *ctx, uint32_t dst_pass_idx, const CmdUseBuffer& cmd)
{
	auto [it, exists] = rg.buffer_passes.emplace(cmd.buffer.id, dst_pass_idx);

	uint32_t src_pass_idx = it->second;

	const ev2::Pass *dst_pass = &rg.passes[dst_pass_idx];

	Buffer *buf = ctx->get_buffer(cmd.buffer);
	ResourceState *p_state = &buf->state;

	ResourceStateFlags src_flags = update_resource_state(p_state, cmd.usage, dst_pass->queue_family);
	ResourceStateFlags dst_flags = p_state->get_current();

	PassBarrier barrier = {
		.buffer = cmd.buffer,
		.src_state = src_flags,
		.dst_state = dst_flags,
		.type = PASS_EDGE_BUFFER,
	};

	update_pass_barriers(rg, barrier, src_pass_idx, dst_pass_idx);
}

void rg_use_image(RenderGraph &rg, GfxContext *ctx, uint32_t dst_pass_idx, const CmdUseImage& cmd)
{
	auto [it, exists] = rg.image_passes.emplace(cmd.image.id, dst_pass_idx);

	uint32_t src_pass_idx = it->second;

	const ev2::Pass *dst_pass = &rg.passes[dst_pass_idx];

	Image *img = ctx->get_image(cmd.image);
	ResourceState *p_state = &img->state;

	ResourceStateFlags src_flags = update_resource_state(p_state, cmd.usage, dst_pass->queue_family);
	ResourceStateFlags dst_flags = p_state->get_current();

	PassBarrier barrier = {
		.image = cmd.image,
		.src_state = src_flags,
		.dst_state = dst_flags,
		.type = PASS_EDGE_IMAGE,
	};

	update_pass_barriers(rg, barrier, src_pass_idx, dst_pass_idx);
}

static PassCommand &rg_new_pass_command(PassParse &parse, const ev2::Command cmd)
{
	uint32_t barrier_count = (uint32_t)parse.barriers.size();
	uint32_t barrier_offset = parse.barrier_offset;

	uint32_t cmd_barrier_count = barrier_count > barrier_offset ? 
		barrier_count - barrier_offset : 0;

	return parse.commands.emplace_back(PassCommand{
		.base = cmd,
		.barrier_count = cmd_barrier_count
	});
}

static void rg_record_pass_cmds(GfxContext *ctx, const PassParse &parse, VkCommandBuffer cmds)
{
}

static void rg_compile(GfxContext *ctx, RenderGraph *rg, uint32_t pass_count, ev2::Pass *passes)
{
	*rg = RenderGraph{
		.ctx = ctx,
		.passes = passes,
	};
	rg->parses.resize(pass_count, PassParse{});

	for (uint32_t pass_idx = 0; pass_idx < pass_count; ++pass_idx) {
		const ev2::Pass &pass = passes[pass_idx];
		PassParse &parse = rg->parses[pass_idx];

		for (const Command &cmd_union : pass.cmds) {
			switch(cmd_union.type) {
				case BindComputePipeline: {
					rg_new_pass_command(parse, cmd_union);
					break;
				}
				case BindGfxPipeline: {
					rg_new_pass_command(parse, cmd_union);
					break;
				}
				case BindResources: {
					rg_new_pass_command(parse, cmd_union);
					break;
				}
				case Dispatch: {
					rg_new_pass_command(parse, cmd_union);
					break;
				}
				case UseBuffer: {
					rg_use_buffer(*rg, ctx, pass_idx, cmd_union.use_buffer);
					break;
				} 
				case UseImage: {
					rg_use_image(*rg, ctx, pass_idx, cmd_union.use_image);
					break;
				}
			}
		}
	}

	rg->command_buffers.resize(pass_count, VK_NULL_HANDLE);

	for (uint32_t pass_idx = 0; pass_idx < pass_count; ++pass_idx) {
		rg_record_pass_cmds(ctx, rg->parses[pass_idx], rg->command_buffers[pass_idx]);
	}

}

void end_frame(GfxContext *ctx)
{
	FrameContext *frame = ctx->get_current_frame();

	uint32_t pass_count = (uint32_t)frame->passes.size();

	RenderGraph lexer;
	rg_compile(ctx, &lexer, pass_count, frame->new_passes.data());

}

};

