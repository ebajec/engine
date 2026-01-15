#include "device_impl.h"
#include "resource_impl.h"

namespace ev2 {

BufferID create_buffer(Device *dev, size_t size, BufferFlags flags)
{
	Buffer buf{};

	GLenum gl_flags = GL_DYNAMIC_STORAGE_BIT;

	if (flags & MAP_READ)
		gl_flags |= GL_MAP_READ_BIT;
	else if (flags & MAP_WRITE)
		gl_flags |= GL_MAP_WRITE_BIT;
	else if (flags & MAP_PERSISTENT)
		gl_flags |= GL_MAP_PERSISTENT_BIT;
	else if (flags & MAP_COHERENT)
		gl_flags |= GL_MAP_COHERENT_BIT;

	glCreateBuffers(1, &buf.id);
	glNamedBufferStorage(buf.id, size, nullptr, gl_flags);

	if (gl_check_err()) {
		return EV2_NULL_HANDLE(Buffer);
	}

	ResourceID id = dev->buffer_pool->allocate(&buf);

	return EV2_HANDLE_CAST(Buffer, id.u64);
}

void destroy_buffer(Device *dev, BufferID h)
{
	ResourceID id = ResourceID{h.id};
	Buffer* buf = dev->buffer_pool->get(id);

	glDeleteBuffers(1, &buf->id);

	dev->buffer_pool->deallocate(id);
}


//------------------------------------------------------------------------------

ImageID create_image(Device *dev, uint32_t w, uint32_t h, uint32_t d, ImageFormat fmt)
{
	Image img {};

	img.w = w;
	img.h = h;
	img.d = d;
	img.fmt = fmt;

	GLenum format, type;
	image_format_to_gl(fmt, &format, &type);

	GLenum internal_format = image_format_to_gl_internal(fmt);

	if (d <= 1) {
		glCreateTextures(GL_TEXTURE_2D, 1, &img.id);
		glTextureStorage2D(img.id, 1, internal_format, w, h);
	} else {
		glCreateTextures(GL_TEXTURE_3D, 1, &img.id);
		glTextureStorage3D(img.id, 1, internal_format, w, h, d);
	}

	if (gl_check_err()) {
		return EV2_NULL_HANDLE(Image);
	}

	ResourceID id = dev->image_pool->allocate(&img);

	return EV2_HANDLE_CAST(Image, id.u64);
}

void destroy_image(Device *dev, ImageID h)
{
	ResourceID id = ResourceID{h.id};
	Image *img = dev->image_pool->get(id);

	glDeleteTextures(1, &img->id);

	dev->image_pool->deallocate(id);
}

TextureID create_texture(Device *dev, ImageID img, TextureFilter filter)
{
	Texture tex {};
	tex.img = img;
	tex.filter = filter;

	ResourceID id = dev->texture_pool->allocate(&tex);
	return EV2_HANDLE_CAST(Texture, id.u64);
}

void destroy_texture(Device *dev, TextureID h)
{
	ResourceID id = {.u64 = h.id};
	dev->texture_pool->deallocate(id);
}

};
