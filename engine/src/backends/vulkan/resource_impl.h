#ifndef EV2_RESOURCE_IMPL_H
#define EV2_RESOURCE_IMPL_H

#include "ev2/resource.h"

#include "def_vulkan.h"
#include "vk_mem_alloc.h"

#include <cstdint>
#include <cstddef>
#include <vector>

struct ResourceSync {
	uint64_t 				wait_value;
	VkSemaphore 			semaphore;
};

struct ResourceStateFlags {
	VkAccessFlags2			access;
	VkPipelineStageFlags2 	stage;
	uint32_t 				queue_family_index;
};

struct ResourceState {
	ResourceStateFlags read;
	ResourceStateFlags write;

	ResourceSync write_sync;
	std::vector<ResourceSync> read_syncs;

	inline ResourceStateFlags get_current() {
		return read.stage ? read : write;
	}

	void get_current_sync(uint32_t *count, const ResourceSync **syncs)
	{
		if (read_syncs.empty()) {
			*count = 1;
			*syncs = &write_sync;
			return;
		} else {
			*count = (uint32_t)read_syncs.size();
			*syncs = read_syncs.data();
		}
		*count = 0;
		*syncs = nullptr;
	}

	inline void sync_read(VkSemaphore semaphore, uint64_t wait_value)
	{
		read_syncs.push_back(ResourceSync{
			.wait_value = wait_value,
			.semaphore = semaphore,
		});
	}

	inline void sync_write(VkSemaphore semaphore, uint64_t wait_value)
	{
		read_syncs.clear();
		write_sync = {
			.wait_value = wait_value,
			.semaphore = semaphore,
		};

		return;
	}

	inline ResourceStateFlags set_read(VkAccessFlags2 access, VkPipelineStageFlags2 stage, 
									uint32_t queue_family)
	{
		read.access |= access;
		read.stage |= stage;
		read.queue_family_index = queue_family;

		return write;
	}

	inline ResourceStateFlags set_write(VkAccessFlags2 access, VkPipelineStageFlags2 stage,
									 uint32_t queue_family)
	{
		ResourceStateFlags old = get_current();

		read.access = 0;
		read.stage = 0;

		write.access = access;
		write.stage = stage;
		write.queue_family_index = queue_family;

		return old;
	}
};

enum ResourceType : uint8_t {
	RESOURCE_TYPE_BUFFER,
	RESOURCE_TYPE_IMAGE
};

union TaggedResource {
	struct {
		uint64_t handle : 48;
		uint64_t type : 16;
	};
	uint64_t u64;

	TaggedResource() {}
	TaggedResource(ev2::BufferID buffer) {
		handle = buffer.id;
		type = RESOURCE_TYPE_BUFFER;
	}

	TaggedResource(ev2::ImageID image) {
		handle = image.id;
		type = RESOURCE_TYPE_IMAGE;
	}

	TaggedResource(uint64_t value) {
		u64 = value;
	}

	operator uint64_t () const {return u64;} 

	constexpr bool operator == (const TaggedResource &other) const {
		return u64 == other.u64;
	}

	constexpr ev2::BufferID to_buffer() const {
		return ev2::BufferID{.id = this->handle};
	}

	constexpr ev2::ImageID to_image() const {
		return ev2::ImageID{.id = this->handle};
	}
};

namespace ev2 {

struct Buffer
{
	VkBuffer buffer;
	VmaAllocation allocation;
	size_t size;

	ResourceState state;
};

struct Image
{
	VkImage image;
	VmaAllocation allocation;

	// optional
	VkImageView base_view = VK_NULL_HANDLE;

	ResourceState state;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkImageAspectFlags aspect_mask;
	uint32_t w;
	uint32_t h;
	uint32_t d;
	uint32_t levels;
	ev2::ImageFormat format;
};

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

