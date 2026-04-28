#include "device_impl.h"
#include "resource_impl.h"

namespace ev2 {

BufferID create_buffer(Device *ctx, size_t size, BufferFlags flags)
{
	GLenum gl_flags = GL_DYNAMIC_STORAGE_BIT;

	if (flags & MAP_READ)
		gl_flags |= GL_MAP_READ_BIT;
	if (flags & MAP_WRITE)
		gl_flags |= GL_MAP_WRITE_BIT;
	if (flags & MAP_PERSISTENT)
		gl_flags |= GL_MAP_PERSISTENT_BIT;
	if (flags & MAP_COHERENT)
		gl_flags |= GL_MAP_COHERENT_BIT;

	Buffer buf {
		.id = 0,
		.flags = gl_flags,
		.size = size,
	};

	glCreateBuffers(1, &buf.id);
	glNamedBufferStorage(buf.id, size, nullptr, gl_flags);

	if (gl_check_err()) {
		return EV2_NULL_HANDLE(Buffer);
	}

	ResourceID id = ctx->buffer_pool->allocate(&buf);

	return EV2_HANDLE_CAST(Buffer, id.u64);
}

void destroy_buffer(Device *ctx, BufferID h)
{
	ResourceID id = ResourceID{h.id};
	Buffer* buf = ctx->buffer_pool->get(id);

	glDeleteBuffers(1, &buf->id);

	ctx->buffer_pool->deallocate(id);
}

uint64_t get_buffer_gpu_handle(Device *ctx, BufferID h)
{
	Buffer *buf = ctx->get_buffer(h);
	return buf->id;
}


//------------------------------------------------------------------------------

ImageID create_image(Device *ctx, uint32_t w, uint32_t h, uint32_t d, ImageFormat fmt, 
					 uint32_t levels)
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
		glTextureStorage2D(img.id, levels, internal_format, w, h);
	} else {
		glCreateTextures(GL_TEXTURE_3D, 1, &img.id);
		glTextureStorage3D(img.id, levels, internal_format, w, h, d);
	}

	if (gl_check_err()) {
		return EV2_NULL_HANDLE(Image);
	}

	ResourceID id = ctx->image_pool->allocate(&img);

	return EV2_HANDLE_CAST(Image, id.u64);
}

void get_image_dims(Device *ctx, ImageID h_img, uint32_t *w, uint32_t *h, uint32_t*d)
{
	Image *img = ctx->get_image(h_img);

	if (w) *w = img->w;
	if (h) *h = img->h;
	if (d) *d = img->d;
}

void destroy_image(Device *ctx, ImageID h)
{
	ResourceID id = ResourceID{h.id};
	Image *img = ctx->image_pool->get(id);

	glDeleteTextures(1, &img->id);

	ctx->image_pool->deallocate(id);
}

uint64_t get_image_gpu_handle(Device *ctx, ImageID h)
{
	Image *img = ctx->get_image(h);
	return img->id;
}

//------------------------------------------------------------------------------
// Uploads

UploadContext begin_upload(Device *ctx, size_t bytes, size_t align)
{
	UploadPool *pool = ctx->pool.get();

	UploadPool::alloc_result_t allocation = pool->alloc(bytes, align);

	return UploadContext {
		.ptr = allocation.ptr,
		.size = bytes,
		.allocation_index = allocation.idx,
	};
}

uint64_t commit_buffer_uploads(Device *ctx, UploadContext ctx, BufferID buf, 
							   const BufferUpload *regions, uint32_t count)
{
	if (!ctx->buffer_pool->check_handle(ResourceID{buf.id}))
		return 0;

	UploadPool *pool = ctx->pool.get();

	return pool->commmit_buffer(ctx.allocation_index, buf, regions, count);
}

uint64_t commit_image_uploads(Device *ctx, UploadContext ctx, ImageID img, 
							  const ImageUpload *regions, uint32_t count)
{
	UploadPool *pool = ctx->pool.get();

	return pool->commmit_image(ctx.allocation_index, img, regions, count);
}

void flush_uploads(Device *ctx) 
{
	UploadPool *pool = ctx->pool.get();

	pool->flush();
}

ev2::Result wait_complete(Device *ctx, uint64_t sync)
{
	return ctx->pool->wait_for(sync);
}

//------------------------------------------------------------------------------
// Textures

TextureID create_texture(Device *ctx, ImageID img, TextureFilter filter)
{
	Texture tex {};
	tex.img = img;
	tex.filter = filter;

	ResourceID id = ctx->texture_pool->allocate(&tex);
	return EV2_HANDLE_CAST(Texture, id.u64);
}

void destroy_texture(Device *ctx, TextureID h)
{
	ResourceID id = {.u64 = h.id};
	ctx->texture_pool->deallocate(id);
}

uint64_t get_texture_gpu_handle(Device *ctx, TextureID h)
{
	Texture *tex = ctx->get_texture(h);
	Image *img = ctx->get_image(tex->img);
	return img->id;
}

void get_texture_dims(Device *ctx, TextureID h_tex, uint32_t *w, uint32_t *h, uint32_t*d)
{
	Texture *tex = ctx->get_texture(h_tex);
	get_image_dims(ctx, tex->img, w, h, d);
}

};
