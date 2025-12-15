#ifndef EV2_RESOURCE_IMPL_H
#define EV2_RESOURCE_IMPL_H

#include <engine/renderer/opengl.h>

#include "ev2/resource.h"

#include <cstdint>
#include <cstddef>

namespace ev2 {

struct Buffer
{
	GLuint id;
	GLenum flags;
	size_t size;
};

struct Image
{
	GLuint id;
	uint32_t w;
	uint32_t h;
	uint32_t d;
	ev2::ImageFormat fmt;
};

struct Texture
{
	ImageID img;
	TextureFilter filter;
};

struct TextureAsset
{
	TextureID tex;
};

};

#endif //EV2_RESOURCE_IMPL_H

