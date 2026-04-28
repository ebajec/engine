#include "context_impl.h"
#include "resource_impl.h"

namespace ev2 {

BufferID create_buffer(GfxContext *ctx, size_t size, BufferUsageFlags flags, size_t align)
{
	Buffer buf {};

	VkBufferCreateInfo buffer_ci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.size = size,
		.usage = flags, 
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
	ResourceID id = ResourceID{h.id};
	Buffer* buf = ctx->buffer_pool->get(id);

	vmaDestroyBuffer(ctx->allocator, buf->buffer, buf->allocation);

	ctx->buffer_pool->deallocate(id);
}

uint64_t get_buffer_gpu_handle(GfxContext *ctx, BufferID h)
{
	Buffer *buf = ctx->get_buffer(h);
	return (uint64_t)buf->buffer;
}

//------------------------------------------------------------------------------

ImageID create_image(GfxContext *ctx, uint32_t w, uint32_t h, uint32_t d, ImageFormat fmt, 
					 uint32_t levels, ImageUsageFlags usage)
{
	Image img {};

	img.w = w;
	img.h = h;
	img.d = d;
	img.fmt = fmt;

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

	VkResult result = vmaCreateImage(ctx->allocator, 
		&img_ci, &alloc_ci, &img.image, &img.allocation, nullptr);

	if (result != VK_SUCCESS)
		return EV2_NULL_HANDLE(Image);

	VkImageViewCreateInfo viewInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.image = img.image,
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

	result = vkCreateImageView(ctx->device, &viewInfo, nullptr, &img.base_view);

	if (result != VK_SUCCESS) {
		vmaDestroyImage(ctx->allocator, img.image, img.allocation);
		return EV2_NULL_HANDLE(Image);
	}

	return ctx->emplace_image(std::move(img));
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
	ResourceID id = ResourceID{h.id};
	Image *img = ctx->image_pool->get(id);

	vmaDestroyImage(ctx->allocator, img->image, img->allocation);

	ctx->image_pool->deallocate(id);
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
	if (!ctx->buffer_pool->check_handle(ResourceID{buf.id}))
		return 0;

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
	Texture tex {};
	tex.img = img;
	tex.filter = filter;

	return ctx->emplace_texture(std::move(tex));
}

void destroy_texture(GfxContext *ctx, TextureID h)
{
	ResourceID id = {.u64 = h.id};
	ctx->texture_pool->deallocate(id);
}

uint64_t get_texture_gpu_handle(GfxContext *ctx, TextureID h)
{
	Texture *tex = ctx->get_texture(h);
	Image *img = ctx->get_image(tex->img);
	return (uint64_t)img->base_view;
}

void get_texture_dims(GfxContext *ctx, TextureID h_tex, uint32_t *w, uint32_t *h, uint32_t*d)
{
	Texture *tex = ctx->get_texture(h_tex);
	get_image_dims(ctx, tex->img, w, h, d);
}

};
