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
