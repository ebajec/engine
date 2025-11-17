#ifndef EV2_RESOURCE_H
#define EV2_RESOURCE_H

#include "ev2/defines.h"
#include "ev2/device.h"

namespace ev2 {

MAKE_HANDLE(Buffer);
MAKE_HANDLE(Image);
MAKE_HANDLE(Texture);

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

//--------------------------------------------------------------------
// Buffer

BufferID create_buffer(Device *dev, size_t size, bool mapped = false);
void destroy_buffer(Device *dev, BufferID buf);

//--------------------------------------------------------------------
// Image

ImageID create_image(Device *dev, uint32_t w, uint32_t h, uint32_t d, ImageFormat fmt);
ImageID load_image(Device *dev, const char *path);
void destroy_image(Device *dev, ImageID img);

//--------------------------------------------------------------------
// Texture

TextureID create_texture(Device *dev, ImageID img, TextureFilter filter);
void destroy_texture(Device *dev, TextureID tex);

};

#endif //EV2_RESOURCE_H
