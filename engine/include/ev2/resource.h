#ifndef EV2_RESOURCE_H
#define EV2_RESOURCE_H

#include "ev2/defines.h"
#include "ev2/context.h"

namespace ev2 {

MAKE_HANDLE(Buffer);
MAKE_HANDLE(Image);
MAKE_HANDLE(Texture);

MAKE_HANDLE(ImageAsset);

enum ImageFormat
{
	IMAGE_FORMAT_RGBA8,
	IMAGE_FORMAT_RGBA32F,
	IMAGE_FORMAT_32F,
	IMAGE_FORMAT_R8_UNORM
};

enum ImageUsage
{
	// These are the vulkan flag bits
    IMAGE_USAGE_TRANSFER_SRC_BIT = 0x00000001,
    IMAGE_USAGE_TRANSFER_DST_BIT = 0x00000002,
    IMAGE_USAGE_SAMPLED_BIT = 0x00000004,
    IMAGE_USAGE_STORAGE_BIT = 0x00000008,
    IMAGE_USAGE_COLOR_ATTACHMENT_BIT = 0x00000010,
    IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT = 0x00000020,
    IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT = 0x00000040,
    IMAGE_USAGE_INPUT_ATTACHMENT_BIT = 0x00000080,
    IMAGE_USAGE_HOST_TRANSFER_BIT = 0x00400000,
};
typedef uint32_t ImageUsageFlags;

enum TextureFilter
{
	FILTER_NEAREST,
	FILTER_BILINEAR,
};

enum BufferUsageFlagBits
{
	// These are the vulkan flag bits
    BUFFER_USAGE_TRANSFER_SRC_BIT = 0x00000001,
    BUFFER_USAGE_TRANSFER_DST_BIT = 0x00000002,
    BUFFER_USAGE_UNIFORM_BUFFER_BIT = 0x00000010,
    BUFFER_USAGE_STORAGE_BUFFER_BIT = 0x00000020,
    BUFFER_USAGE_INDEX_BUFFER_BIT = 0x00000040,
    BUFFER_USAGE_VERTEX_BUFFER_BIT = 0x00000080,
    BUFFER_USAGE_INDIRECT_BUFFER_BIT = 0x00000100,
};
typedef uint32_t BufferUsageFlags;

struct ImageUpload
{
	size_t src_offset;
	uint32_t x, y, z;
	uint32_t w, h, d{1};
	uint32_t level;
};

struct BufferUpload
{
	size_t src_offset = 0;
	size_t dst_offset = 0;
	size_t size;
};

struct UploadContext
{
	void *ptr;
	size_t size;
	uint32_t allocation_index;
};

UploadContext begin_upload(GfxContext *ctx, size_t bytes, size_t align);
void flush_uploads(GfxContext * ctx);

// @brief Schedule uploads to be executed on next flush of the corresponding
// upload pool.
//
// @return Counter value reached by pool upon completion of the upload.
uint64_t commit_buffer_uploads(GfxContext *ctx, UploadContext uc, BufferID buf, 
							   const BufferUpload *uploads, uint32_t count);
uint64_t commit_image_uploads(GfxContext *ctx, UploadContext uc, ImageID img, 
							  const ImageUpload *uploads, uint32_t count);

ev2::Result wait_complete(GfxContext *ctx, uint64_t sync);


//--------------------------------------------------------------------
// Buffer

BufferID create_buffer(GfxContext *ctx, size_t size, BufferUsageFlags usage = 0, 
					   size_t align = 0);
void destroy_buffer(GfxContext *ctx, BufferID buf);
uint64_t get_buffer_gpu_handle(GfxContext *ctx, BufferID h);

//--------------------------------------------------------------------
// Image

ImageID create_image(GfxContext *ctx, uint32_t w, uint32_t h, uint32_t d, 
					 ImageFormat fmt, uint32_t levels = 1, ImageUsageFlags usage = 0);
void destroy_image(GfxContext *ctx, ImageID img);

void get_image_dims(GfxContext *ctx, ImageID h_img, uint32_t *w, uint32_t *h, uint32_t*d);
uint64_t get_image_gpu_handle(GfxContext *ctx, ImageID img);

//--------------------------------------------------------------------
// Texture

TextureID create_texture(GfxContext *ctx, ImageID img, TextureFilter filter);
void destroy_texture(GfxContext *ctx, TextureID tex);
uint64_t get_texture_gpu_handle(GfxContext *ctx, TextureID tex);

void get_texture_dims(GfxContext *ctx, TextureID tex, uint32_t *w, uint32_t *h, uint32_t*d);

//------------------------------------------------------------------------------
// Image assets

ImageAssetID load_image_asset(GfxContext *ctx, const char *path);
void unload_image_asset(GfxContext *ctx, ImageAssetID id);
ImageID get_image_resource(GfxContext *ctx, ImageAssetID id);

};

#endif //EV2_RESOURCE_H
