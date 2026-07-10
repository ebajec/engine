#ifndef EV2_RESOURCE_IMPL_H
#define EV2_RESOURCE_IMPL_H

#include "ev2/resource.h"
#include "utils/common.h"

#include <robin_hood.h>

#include "def_vulkan.h"
#include "vk_mem_alloc.h"

#include <cstdint>
#include <cstddef>
#include <cassert>
#include <cstring>
#include <vector>

namespace ev2 {

struct ResourceSync {
	uint64_t 				wait_value;
	VkSemaphore 			semaphore;
};

struct ResourceStateFlags {
	VkAccessFlags2			access;
	VkPipelineStageFlags2 	stage;
	uint32_t 				queue_family_index;
	VkImageLayout 			layout;
};

struct ResourceState {
	ResourceStateFlags read;
	ResourceStateFlags write;

	ResourceSync write_sync;
	robin_hood::unordered_flat_map<VkSemaphore, uint64_t> read_syncs;

	uint64_t last_used_by_frame : 63;
	bool written : 1;

	inline ResourceStateFlags get_current() {
		return read.stage ? read : write;
	}

	// @brief Return the syncs for the work that needs to complete
	// before the next write.
	void get_wait_syncs_for_write(std::vector<ResourceSync> &out)
	{
		if (!read_syncs.empty()) {
			out.reserve(read_syncs.size());
			for (auto [sem, wait] : read_syncs) {
				out.push_back(ResourceSync{
					.wait_value = wait,
					.semaphore = sem,
				});
			}
			return;
		} else if (write_sync.semaphore) {
			out.push_back(write_sync);
			return;
		}
	}

	// @brief Return the syncs for the work that needs to complete
	// before the next write.
	void get_wait_syncs_for_read(uint32_t *count, const ResourceSync **syncs)
	{
		if (write_sync.semaphore) {
			*count = 1;
			*syncs = &write_sync;
			return;
		}
		*count = 0;
		*syncs = nullptr;
	}

	// @brief Update the synchronization state of a resource as being 
	// read by a submission which signals semaphore and wait_value
	inline void sync_read(VkSemaphore semaphore, uint64_t wait_value)
	{
		assert(semaphore);

		auto [it, inserted] = read_syncs.emplace(semaphore, wait_value);

		if (!inserted) {
			assert(wait_value > it->second);
		}
	}

	// @brief Update the synchronization state of a resource as being 
	// written by a submission which signals semaphore and wait_value
	inline void sync_write(VkSemaphore semaphore, uint64_t wait_value)
	{
		assert(semaphore);

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
		read.layout = VK_IMAGE_LAYOUT_GENERAL;

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
		write.layout = VK_IMAGE_LAYOUT_GENERAL;

		written = true;

		return old;
	}
};

enum ResourceType : uint8_t {
	RESOURCE_TYPE_BUFFER,
	RESOURCE_TYPE_IMAGE
};

union TaggedResource {
	struct {
		uint64_t handle : 32;
		uint64_t generation : 16;
		uint64_t type : 16;
	};
	uint64_t u64;

	TaggedResource() {}
	TaggedResource(ev2::BufferID buffer) {
		handle = buffer.id;
		generation = buffer.gen;
		type = RESOURCE_TYPE_BUFFER;
	}

	TaggedResource(ev2::ImageID image) {
		handle = image.id;
		generation = image.gen;
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
		return ev2::BufferID{.id = handle, .gen = generation};
	}

	constexpr ev2::ImageID to_image() const {
		return ev2::ImageID{.id = handle, .gen = generation};
	}

	uint32_t id() const {
		switch(type) {
			case RESOURCE_TYPE_BUFFER: return to_buffer().id;
			case RESOURCE_TYPE_IMAGE: return to_image().id;
			default: return 0;
		}
	}

	const char *type_str() const {
		switch(type) {
			case RESOURCE_TYPE_BUFFER: return "Buffer";
			case RESOURCE_TYPE_IMAGE: return "Image";
			default: return "";
		}
	}
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wbitfield-enum-conversion"
struct ImageViewKey
{
	// This produces a warning because the vulkan spec
	// puts MAX_ENUM at 0x7FFFFFFF
    VkImageViewType 	type : 16;

	// This produces a warning because the vulkan spec
	// puts MAX_ENUM at 0x7FFFFFFF
    VkImageAspectFlags 	aspectMask : 16;

    uint32_t 			baseMipLevel;
    uint32_t 			levelCount;
    uint32_t 			baseArrayLayer;
    uint32_t 			layerCount;
	VkFormat 			format;

	constexpr bool operator == (const ImageViewKey &other) const {
		return 
			type == other.type &&
			baseMipLevel == other.baseMipLevel &&
			levelCount == other.levelCount &&
			baseArrayLayer == other.baseArrayLayer && 
			layerCount == other.layerCount && 
			format == other.format;
	};

	struct Hash {
		inline size_t operator()(const ImageViewKey &key) const {
			size_t h1 = hash_combine(
				robin_hood::hash<uint64_t>{}(
					(uint64_t)key.baseMipLevel << 32 | 
					(uint64_t)key.levelCount
				), 
				robin_hood::hash<uint64_t>{}(
					(uint64_t)key.baseArrayLayer << 32 | 
					(uint64_t)key.layerCount
				)
			);

			size_t h2 = robin_hood::hash<uint64_t>{}(
				(uint64_t)key.format << 32 | 
				(uint64_t) key.aspectMask << 16 | 
				(uint64_t) key.type
			);

			return hash_combine(h1, h2);
		}
	};
};
#pragma clang diagnostic pop

//------------------------------------------------------------------------------

struct Buffer
{
	ResourceState state;

	VkBuffer buffer;
	VmaAllocation allocation;
	size_t size;
};

struct Image
{
	ResourceState state;

	VkImage image;
	VmaAllocation allocation;

	robin_hood::unordered_flat_map<
		ImageViewKey, 
		VkImageView, 
		ImageViewKey::Hash
	> view_cache;

	VkImageAspectFlags aspect_mask;
	VkFormat format;

	uint32_t w;
	uint32_t h;
	uint32_t d;
	uint32_t levels;

	VkImageSubresourceRange whole_image_range() {
		return VkImageSubresourceRange{
			.aspectMask = aspect_mask,
			.baseMipLevel = 0,
			.levelCount = levels,
			.baseArrayLayer = 0, 
			.layerCount = d,
		};
	}
};

struct Texture
{
	ImageID img;
	TextureFilter filter;
	VkSampler sampler;
	VkImageView view;
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

extern VkImageView get_image_view(GfxContext *ctx, Image *image, const ImageViewKey &key);

};

#endif //EV2_RESOURCE_IMPL_H

