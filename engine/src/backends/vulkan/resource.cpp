#include "context.h"
#include "resource.h"

#include "imgui/inspector.h"

namespace ev2 {

BufferID create_buffer(GfxContext *ctx, size_t size, BufferUsageFlags usage, size_t align)
{
	Buffer buf {};

	if (!usage) {
		log_error("buffer usage flags cannot be 0!");
		return EV2_NULL_HANDLE(Buffer);
	}

	usage |= ev2::BUFFER_USAGE_TRANSFER_DST_BIT;

	VkBufferCreateInfo buffer_ci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.size = size,
		.usage = usage, 
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = nullptr
	};

	VmaAllocationCreateInfo alloc_ci= {
		.flags = 0,
		.usage = VMA_MEMORY_USAGE_AUTO,
		.requiredFlags = 0,
		.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		.minAlignment = align,
	};

	VmaAllocationInfo alloc_info;

	VkResult result = vmaCreateBuffer(
		ctx->allocator, &buffer_ci, &alloc_ci, &buf.buffer, &buf.allocation, &alloc_info);  

	if (result != VK_SUCCESS) {
		return EV2_NULL_HANDLE(Buffer);
	}

	buf.size = size;

	BufferID id = ctx->emplace_buffer(std::move(buf)); 
	return id; 
}

void destroy_buffer_internal(GfxContext *ctx, TaggedResource resource)
{
	Buffer* buf = ctx->get_buffer(resource.to_buffer());

	vmaDestroyBuffer(ctx->allocator, buf->buffer, buf->allocation);
	ctx->buffer_pool->deallocate(to_pool_id(resource.to_buffer()));
}
void destroy_buffer(GfxContext *ctx, BufferID h)
{
	ctx->queue_delete(h, &destroy_buffer_internal);
}

uint64_t get_buffer_gpu_handle(GfxContext *ctx, BufferID h)
{
	Buffer *buf = ctx->get_buffer(h);
	return (uint64_t)buf->buffer;
}

//------------------------------------------------------------------------------

ImageID create_image(GfxContext *ctx, uint32_t w, uint32_t h, uint32_t d, ImageFormat fmt, ImageUsageFlags usage, 
					 uint32_t levels)
{
	if (!usage) {
		log_error("image usage flags cannot be 0!");
		return EV2_NULL_HANDLE(Image);
	}

	usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	if (g_vk.allow_resource_inspection) {
		usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	}

	VkImageType type = VK_IMAGE_TYPE_2D;
	VkFormat format = image_format_to_vk(fmt); 

	VkImageCreateInfo img_ci = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.imageType = type,
		.format = format,
		.extent = {
			.width = w,
			.height = h,
			.depth = 1,
		},
		.mipLevels = levels,
		.arrayLayers = d,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices = nullptr,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VmaAllocationCreateInfo alloc_ci= {
		.flags = 0,
		.usage = VMA_MEMORY_USAGE_AUTO,
		.requiredFlags = 0,
		.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	};

	VkImage image;
	VmaAllocation allocation;

	VkResult result = vmaCreateImage(ctx->allocator, 
		&img_ci, &alloc_ci, &image, &allocation, nullptr);

	if (result != VK_SUCCESS)
		return EV2_NULL_HANDLE(Image);

	return ctx->emplace_image(Image{
		.image = image,
		.allocation = allocation,
		.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
		.format = format,
		.w = w,
		.h = h, 
		.d = d,
		.levels = levels,
	});
}

void get_image_dims(GfxContext *ctx, ImageID h_img, uint32_t *w, uint32_t *h, 
					uint32_t *d, uint32_t *levels)
{
	Image *img = ctx->get_image(h_img);

	if (w) *w = img->w;
	if (h) *h = img->h;
	if (d) *d = img->d;
	if (levels) *levels = img->levels;
}

void set_image_name(GfxContext *ctx, ImageID h, const char *name)
{
	Image *img = ctx->get_image(h);
	img->name = g_register_string(name);
}

const char *get_image_name(GfxContext *ctx, ImageID h)
{
	Image *img = ctx->get_image(h);
	return img->name;
}

void pre_destroy_callback(GfxContext *ctx, ImageID img,
						  std::function<void()> &&callback)
{
	ctx->deferred_delete.callbacks[img] = std::move(callback);
}

void destroy_image_internal(GfxContext *ctx, TaggedResource resource)
{
	Image *img = ctx->get_image(resource.to_image());

	for (const auto &[key, view] : img->view_cache) {
		vkDestroyImageView(ctx->device, view, nullptr);
	}

	vmaDestroyImage(ctx->allocator, img->image, img->allocation);

	ctx->image_pool->deallocate(to_pool_id(resource.to_image()));
}

void destroy_image(GfxContext *ctx, ImageID h)
{
#ifdef EV2_ENABLE_IMGUI
	ev2::imgui::on_destroy_image(h);
#endif
	ctx->queue_delete(h, &destroy_image_internal);
}

//------------------------------------------------------------------------------
// Uploads

UploadContext begin_upload(GfxContext *ctx, size_t bytes, size_t align)
{
	UploadPool *pool = ctx->pool.get();

	UploadPool::alloc_result_t allocation = pool->alloc(bytes, align);

	return UploadContext {
		.ptr = allocation.ptr,
		.size = bytes,
		.allocation_index = allocation.idx,
	};
}

uint64_t commit_buffer_uploads(GfxContext *ctx, UploadContext uc, BufferID buf, 
							   const BufferUpload *regions, uint32_t count)
{
	UploadPool *pool = ctx->pool.get();
	return pool->commit_buffer(uc.allocation_index, buf, regions, count);
}

uint64_t commit_image_uploads(GfxContext *ctx, UploadContext uc, ImageID img, 
							  const ImageUpload *regions, uint32_t count)
{
	UploadPool *pool = ctx->pool.get();
	return pool->commit_image(uc.allocation_index, img, regions, count); 
}

void flush_uploads(GfxContext *ctx) 
{
	ctx->pool->flush();
}

ev2::Result wait_complete(GfxContext *ctx, uint64_t sync)
{
	return ctx->pool->wait_for(sync);
}

//------------------------------------------------------------------------------
// Textures

TextureID create_texture(GfxContext *ctx, ImageID img, TextureFilter filter,
						 uint32_t level, uint32_t layer)
{
	TextureID out_id = ctx->emplace_texture(Texture{});

	if (out_id.id >= EV2_MAX_BINDLESS_DESCRIPTORS) {
		log_error("Number of created textures exceeds maximum (%d)", 
			EV2_MAX_BINDLESS_DESCRIPTORS);
		return EV2_NULL_HANDLE(Texture);
	}

	Image *image = ctx->get_image(img);

	ImageViewKey view_key = {
		.type = VK_IMAGE_VIEW_TYPE_2D,
		.aspectMask = image->aspect_mask,
		.baseMipLevel = level,
		.levelCount = VK_REMAINING_MIP_LEVELS,
		.baseArrayLayer = layer,
		.layerCount = 1,
		.format = image->format,
	};

	VkImageView view = get_image_view(ctx, image, view_key);

	VkSampler sampler;

	VkFilter min_filt, mag_filt;

	switch(filter) {
		case ev2::FILTER_BILINEAR:
			min_filt = VK_FILTER_LINEAR;
			mag_filt = VK_FILTER_LINEAR;
			break;
		case ev2::FILTER_NEAREST:
			min_filt = VK_FILTER_NEAREST;
			mag_filt = VK_FILTER_NEAREST;
			break;
	}

	VkSamplerCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.flags = 0,
		.magFilter = mag_filt,
		.minFilter = min_filt,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.mipLodBias = 0.f,
		.anisotropyEnable = false,
		.compareEnable = false,
		.compareOp = VK_COMPARE_OP_NEVER,
		.minLod = 0.f,
		.maxLod = VK_LOD_CLAMP_NONE,
		.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
		.unnormalizedCoordinates = false,
	};

	VkResult result = vkCreateSampler(ctx->device, &create_info, nullptr, &sampler);

	if (result != VK_SUCCESS)
		return EV2_NULL_HANDLE(Texture);

	Texture *texture = ctx->get_texture(out_id);

	*texture = Texture{
		.img = img,
		.filter = filter,
		.sampler = sampler,
		.view = view
	};

	// The slot in the bindless array is keyed by the id in the pool
	
	VkDescriptorImageInfo image_info = {
		.sampler = sampler,
		.imageView = view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	assert(out_id.id < EV2_MAX_BINDLESS_DESCRIPTORS);

	VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = ctx->bindless_set,
		.dstBinding = 0,
		.dstArrayElement = get_bindless_handle(ctx, out_id),
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &image_info,
	};

	vkUpdateDescriptorSets(ctx->device, 1, &write, 0, 0);

	return out_id;
}

static void destroy_texture_internal(GfxContext *ctx, TaggedResource resource)
{
	TextureID tex_id = resource.to_texture();
	Texture *tex = ctx->get_texture(tex_id);

	// TODO: Replace with dummy texture
	//
	//VkDescriptorImageInfo image_info = {
	//	.sampler = VK_NULL_HANDLE,
	//	.imageView = VK_NULL_HANDLE,
	//	.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	//};

	//VkWriteDescriptorSet write = {
	//	.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	//	.dstSet = ctx->bindless_set,
	//	.dstBinding = 0,
	//	.dstArrayElement = get_bindless_handle(ctx, tex_id),
	//	.descriptorCount = 1,
	//	.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	//	.pImageInfo = &image_info,
	//};

	//vkUpdateDescriptorSets(ctx->device, 1, &write, 0, 0);

	vkDestroySampler(ctx->device, tex->sampler, nullptr);
	ctx->texture_pool->deallocate(to_pool_id(tex_id));
}

uint32_t get_bindless_handle(GfxContext *ctx, TextureID h)
{
	if (!h.is_valid())
		return 0;

	return h.id - 1;
}

void destroy_texture(GfxContext *ctx, TextureID h)
{
	ctx->queue_delete(h, &destroy_texture_internal);
}

ImageID get_backing_image(GfxContext *ctx, TextureID h)
{
	Texture *tex = ctx->get_texture(h);
	return tex->img;
}

void get_texture_gpu_handle(GfxContext *ctx, TextureID h, VkImageView *view)
{
	Texture *tex = ctx->get_texture(h);
	*view = tex->view;
}

void get_texture_dims(GfxContext *ctx, TextureID h_tex, uint32_t *w, uint32_t *h, uint32_t*d)
{
	Texture *tex = ctx->get_texture(h_tex);
	get_image_dims(ctx, tex->img, w, h, d);
}

//-----------------------------------------------------------------------------
// Image view caching + creation

VkImageView get_image_view(GfxContext *ctx, Image *image, const ImageViewKey &key)
{
	assert(key.format == image->format);
	assert(key.aspectMask & image->aspect_mask);
	assert(
		key.levelCount == VK_REMAINING_MIP_LEVELS || 
		key.baseMipLevel + key.levelCount <= image->levels
	);
	assert(
		key.layerCount == VK_REMAINING_ARRAY_LAYERS ||
		key.baseArrayLayer + key.layerCount <= image->d
	);

	auto [it, inserted] = image->view_cache.emplace(key, VK_NULL_HANDLE);

	VkImageView &out_view = it->second;

	if (!inserted) {
		return out_view;
	}

	VkImageAspectFlags aspect_mask = key.aspectMask;

	if ((aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT) && 
		(aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT)) {
		aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	VkImageViewCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.flags = 0,
		.image = image->image,
		.viewType = key.type,
		.format = key.format,
		.subresourceRange = VkImageSubresourceRange{
			.aspectMask = aspect_mask,
			.baseMipLevel = key.baseMipLevel,
			.levelCount = key.levelCount,
			.baseArrayLayer = key.baseArrayLayer,
			.layerCount = key.layerCount,
		},
	};

	VkResult result = vkCreateImageView(ctx->device, &create_info, nullptr, &out_view);
	if (result != VK_SUCCESS) {
		log_error("Failed to create image view");
		return VK_NULL_HANDLE;
	}

	return out_view;
}

};
