#ifndef EV2_RESOURCE_H
#define EV2_RESOURCE_H

#include "ev2/defines.h"
#include "ev2/device.h"

namespace ev2 {

MAKE_HANDLE(Buffer);
MAKE_HANDLE(Image);
MAKE_HANDLE(Texture);

MAKE_HANDLE(ImageAsset);

enum ImageFormat
{
	IMAGE_FORMAT_RGBA8,
	IMAGE_FORMAT_32F
};

enum TextureFilter
{
	FILTER_NEAREST,
	FILTER_BILINEAR,
};

enum BufferFlagBits
{
	MAP_READ = 0x1,
	MAP_WRITE = 0x2,
	MAP_PERSISTENT = 0x4,
	MAP_COHERENT = 0x8,
};
typedef uint32_t BufferFlags;

struct ImageUpload
{
	size_t src_offset;
	uint32_t x, y, z;
	uint32_t w, h, d{1};
	uint32_t level;
};

struct BufferUpload
{
	size_t src_offset;
	size_t dst_offset;
	size_t size;
};

struct UploadContext
{
	void *ptr;
	size_t size;
	uint32_t idx;
};

UploadContext begin_upload(Device *dev, size_t bytes, size_t align);
void flush_uploads(Device * dev);

uint64_t commit_buffer_uploads(Device *dev, UploadContext ctx, BufferID buf, 
							   const BufferUpload *uploads, uint32_t count);
uint64_t commit_image_uploads(Device *dev, UploadContext ctx, ImageID img, 
							  const ImageUpload *uploads, uint32_t count);


//--------------------------------------------------------------------
// Buffer

BufferID create_buffer(Device *dev, size_t size, BufferFlags flags = 0);
void destroy_buffer(Device *dev, BufferID buf);

//--------------------------------------------------------------------
// Image

ImageID create_image(Device *dev, uint32_t w, uint32_t h, uint32_t d, ImageFormat fmt);
void destroy_image(Device *dev, ImageID img);

//--------------------------------------------------------------------
// Texture

TextureID create_texture(Device *dev, ImageID img, TextureFilter filter);
void destroy_texture(Device *dev, TextureID tex);

//------------------------------------------------------------------------------
// Image assets

ImageAssetID load_image_asset(Device *dev, const char *path);
void unload_image_asset(Device *dev, ImageAssetID id);
ImageID get_image_resource(Device *dev, ImageAssetID id);

};

#endif //EV2_RESOURCE_H
