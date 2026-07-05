#include "context_impl.h"
#include "resource_impl.h"

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

	return ctx->emplace_buffer(std::move(buf));
}

void destroy_buffer(GfxContext *ctx, BufferID h)
{
	Buffer* buf = ctx->get_buffer(h);
	vmaDestroyBuffer(ctx->allocator, buf->buffer, buf->allocation);

	ctx->buffer_pool->deallocate(to_pool_id(h));
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

	VkImageType type = d <= 1 ?  
		VK_IMAGE_TYPE_2D : VK_IMAGE_TYPE_3D;

	VkImageCreateInfo img_ci = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.imageType = type,
		.format = image_format_to_vk(fmt),
		.extent = {
			.width = w,
			.height = h,
			.depth = d,
		},
		.mipLevels = levels,
		.arrayLayers = 1,
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

	VkImageView image_view;

	VkImageViewCreateInfo viewInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.image = image,
		.viewType = (VkImageViewType)img_ci.imageType,
		.format = img_ci.format,
		.components = {},
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = levels,
			.baseArrayLayer = 0,
			.layerCount = d,
		},
	};

	result = vkCreateImageView(ctx->device, &viewInfo, nullptr, &image_view);

	if (result != VK_SUCCESS) {
		vmaDestroyImage(ctx->allocator, image, allocation);
		return EV2_NULL_HANDLE(Image);
	}

	return ctx->emplace_image(Image{
		.image = image,
		.allocation = allocation,
		.base_view = image_view,
		.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
		.w = w,
		.h = h, 
		.d = d,
		.levels = levels,
		.format = fmt,
	});
}

void get_image_dims(GfxContext *ctx, ImageID h_img, uint32_t *w, uint32_t *h, uint32_t*d)
{
	Image *img = ctx->get_image(h_img);

	if (w) *w = img->w;
	if (h) *h = img->h;
	if (d) *d = img->d;
}

void destroy_image(GfxContext *ctx, ImageID h)
{
	Image *img = ctx->get_image(h);

	vmaDestroyImage(ctx->allocator, img->image, img->allocation);

	ctx->image_pool->deallocate(to_pool_id(h));
}

uint64_t get_image_gpu_handle(GfxContext *ctx, ImageID h)
{
	Image *img = ctx->get_image(h);
	return (uint64_t)img->image;
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
	return pool->commmit_buffer(uc.allocation_index, buf, regions, count);
}

uint64_t commit_image_uploads(GfxContext *ctx, UploadContext uc, ImageID img, 
							  const ImageUpload *regions, uint32_t count)
{
	UploadPool *pool = ctx->pool.get();
	return pool->commmit_image(uc.allocation_index, img, regions, count); 
}

void flush_uploads(GfxContext *ctx) 
{
	UploadPool *pool = ctx->pool.get();
	pool->flush();
}

ev2::Result wait_complete(GfxContext *ctx, uint64_t sync)
{
	return ctx->pool->wait_for(sync);
}

//------------------------------------------------------------------------------
// Textures

TextureID create_texture(GfxContext *ctx, ImageID img, TextureFilter filter)
{
	VkSampler sampler;

	VkSamplerCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.flags = 0,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.mipLodBias = 0.f,
		.anisotropyEnable = false,
		.compareEnable = false,
		.compareOp = VK_COMPARE_OP_NEVER,
		.minLod = 0.f,
		.maxLod = 0.f,
		.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
		.unnormalizedCoordinates = false,
	};

	VkResult result = vkCreateSampler(ctx->device, &create_info, nullptr, &sampler);

	if (result != VK_SUCCESS)
		return EV2_NULL_HANDLE(Texture);

	return ctx->emplace_texture(Texture{
		.img = img,
		.filter = filter,
		.sampler = sampler
	});
}

void destroy_texture(GfxContext *ctx, TextureID h)
{
	ctx->texture_pool->deallocate(to_pool_id(h));
}

void get_texture_gpu_handle(GfxContext *ctx, TextureID h, VkImageView *view)
{
	Texture *tex = ctx->get_texture(h);
	Image *img = ctx->get_image(tex->img);

	*view = img->base_view;
}

void get_texture_dims(GfxContext *ctx, TextureID h_tex, uint32_t *w, uint32_t *h, uint32_t*d)
{
	Texture *tex = ctx->get_texture(h_tex);
	get_image_dims(ctx, tex->img, w, h, d);
}

};
