#ifndef EV2_RESOURCE_IMPL_H
#define EV2_RESOURCE_IMPL_H

#include "ev2/resource.h"

#include "def_vulkan.h"
#include "vk_mem_alloc.h"

#include <cstdint>
#include <cstddef>

struct ResourceSyncInfo {
	uint64_t 				wait_value;
	VkSemaphore 			semaphore;

	VkAccessFlags2			access;
	VkPipelineStageFlags2 	stage;
	uint32_t 				queue_family_index;
};

namespace ev2 {

struct Buffer
{
	VkBuffer buffer;
	VmaAllocation allocation;
	size_t size;

	ResourceSyncInfo sync;
};

struct Image
{
	VkImage image;
	VmaAllocation allocation;

	// optional
	VkImageView base_view = VK_NULL_HANDLE;

	ResourceSyncInfo sync;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkImageAspectFlags aspect_mask;
	uint32_t w;
	uint32_t h;
	uint32_t d;
	uint32_t levels;
	ev2::ImageFormat fmt;
};

static VkBufferMemoryBarrier2 use_buffer(Buffer *buffer, 
								 VkAccessFlags2 access,
								 VkPipelineStageFlags2 stage,
								 uint32_t src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
								 uint32_t dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED
								 )
{
	VkBufferMemoryBarrier2 barrier = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
		.pNext = nullptr, 
		.srcStageMask = buffer->sync.stage,
		.srcAccessMask = buffer->sync.access,
		.dstStageMask = stage,
		.dstAccessMask = access,
		.srcQueueFamilyIndex = src_queue_family_index,
		.dstQueueFamilyIndex = dst_queue_family_index,
		.buffer = buffer->buffer,
		.offset = 0,
		.size = buffer->size,
	};

	buffer->sync.stage = stage;
	buffer->sync.access = access;

	if (dst_queue_family_index != VK_QUEUE_FAMILY_IGNORED)
		buffer->sync.queue_family_index = dst_queue_family_index;

	return barrier;
}

static VkImageMemoryBarrier2 use_image(Image *img,
								 VkAccessFlags2 access,
								 VkPipelineStageFlags2 stage,
								 VkImageSubresourceRange subresource,
								 VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL,
								 uint32_t src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
								 uint32_t dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED
								 )
{
	VkImageMemoryBarrier2 barrier = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
		.pNext = nullptr, 
		.srcStageMask = img->sync.stage,
		.srcAccessMask = img->sync.access,
		.dstStageMask = stage,
		.dstAccessMask = access,
		.oldLayout = img->layout,
		.newLayout = layout,
		.srcQueueFamilyIndex = src_queue_family_index,
		.dstQueueFamilyIndex = dst_queue_family_index,
		.image = img->image,
		.subresourceRange = subresource,
	};

	img->sync.stage = stage;
	img->sync.access = access;
	img->layout = layout;

	if (dst_queue_family_index != VK_QUEUE_FAMILY_IGNORED)
		img->sync.queue_family_index = dst_queue_family_index;

	return barrier;
}

struct Texture
{
	ImageID img;
	TextureFilter filter;
};

struct ImageAsset
{
	ImageID img;
};

static inline VkFormat image_format_to_vk(ev2::ImageFormat fmt)
{
	switch (fmt) {
		case ev2::IMAGE_FORMAT_RGBA8:
			return VK_FORMAT_R8G8B8A8_UNORM;
		case ev2::IMAGE_FORMAT_RGBA32F:
			return VK_FORMAT_R32G32B32A32_SFLOAT;
		case ev2::IMAGE_FORMAT_32F:
			return VK_FORMAT_R32_SFLOAT;
		case ev2::IMAGE_FORMAT_R8_UNORM:
			return VK_FORMAT_R8_UNORM;
	}
}

};

#endif //EV2_RESOURCE_IMPL_H

