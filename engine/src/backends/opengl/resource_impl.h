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

struct ImageAsset
{
	ImageID img;
};

static inline void image_format_to_gl(ev2::ImageFormat fmt, GLenum *format, GLenum *type)
{
	switch (fmt) {
		case ev2::IMAGE_FORMAT_RGBA8:
			*format = GL_RGBA;
			*type = GL_UNSIGNED_BYTE;
		break;
		case ev2::IMAGE_FORMAT_RGBA32F:
			*format = GL_RGBA;
			*type = GL_FLOAT;
		break;
		case ev2::IMAGE_FORMAT_32F:
			*format = GL_RED;
			*type = GL_FLOAT;
		break;
		case ev2::IMAGE_FORMAT_R8_UNORM:
			*format = GL_RED;
			*type = GL_UNSIGNED_BYTE;
		break;
	}
	return;
}

static inline GLenum image_format_to_gl_internal(ev2::ImageFormat fmt)
{
	switch (fmt) {
		case ev2::IMAGE_FORMAT_RGBA8:
			return GL_RGBA8;
		break;
		case ev2::IMAGE_FORMAT_RGBA32F:
			return GL_RGBA32F;
		break;
		case ev2::IMAGE_FORMAT_32F:
			return GL_R32F;
		break;
		case ev2::IMAGE_FORMAT_R8_UNORM:
			return GL_R8; 
	}
	return GL_RGBA8;
}


};

#endif //EV2_RESOURCE_IMPL_H

