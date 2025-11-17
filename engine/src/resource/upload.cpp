#include "resource/upload.h"
#include "resource/buffer.h"
#include "resource/texture_loader.h"

#include "renderer/gl_debug.h"

#include <cstring>

struct UploadContext
{
	ResourceTable *rt;
};

//------------------------------------------------------------------------------
// OpenGL backend

UploadResult 
get_gl_buffer_mapped_span(UploadSpan *span, GLBuffer *buf, const UploadParams *params)
{
	GLint map_align = 0;
	glGetIntegerv(GL_MIN_MAP_BUFFER_ALIGNMENT, &map_align);

	void *ptr = glMapNamedBufferRange(
			buf->id, 
			0, 
			buf->size,
			GL_MAP_WRITE_BIT | 
			GL_MAP_PERSISTENT_BIT | 
			GL_MAP_COHERENT_BIT
	);

	if (gl_check_err())
		return UPLOAD_EUNKNOWN;

	*span = {
		.ptr = (char*)ptr,
		.size = buf->size,
		.align = (size_t)map_align,
		.token = buf->id
	};

	return UPLOAD_OK;
}

GLuint get_gl_staging_pbo(size_t size, void** p_map)
{
	GLuint pbo = 0;
	glCreateBuffers(1,&pbo);
	glNamedBufferStorage(
		pbo,
		(GLsizeiptr)size,
		nullptr,
		GL_MAP_WRITE_BIT | 
		GL_MAP_PERSISTENT_BIT | 
		GL_MAP_COHERENT_BIT | 
		GL_DYNAMIC_STORAGE_BIT
	);

	void *mapped = nullptr;

	if (gl_check_err())
		goto failure;

	mapped = glMapNamedBufferRange(
		pbo, 
		0, 
		(GLsizeiptr)size, 
		GL_MAP_WRITE_BIT | 
		GL_MAP_PERSISTENT_BIT | 
		GL_MAP_COHERENT_BIT
	);

	if (!mapped || gl_check_err())
		goto failure;

	*p_map = mapped;

	return pbo;
failure:
	if (pbo)
		glDeleteBuffers(1,&pbo);
	return 0;
}

void release_gl_staging_pbo(GLuint pbo)
{
	glUnmapNamedBuffer(pbo);
	glDeleteBuffers(1,&pbo);
}

void buffer_upload_end(
	UploadSession *s
)
{
	GLBuffer *buf = static_cast<GLBuffer*>(s->ent->data);

	UploadMode mode = s->mode;

	switch (mode) {
		case UPLOAD_MODE_MAPPED:
			glUnmapNamedBuffer(buf->id);
		case UPLOAD_MODE_STAGING:
		default:
			return;
	}

	return;
}

struct GLStagedImageUploader
{
	GLuint pbo;
};

UploadResult init_staged_image_upload(
	UploadSession *s, 
	const ImageUploadRegion *regions, 
	GLImage *img
)
{
	size_t pix_size = img_format_to_bytes(img->fmt);

	size_t req = 0;

	for (size_t i = 0; i < s->span_count; ++i) {
		ImageUploadRegion reg = regions[i];
		size_t size = pix_size*(size_t)reg.w*(size_t)reg.h*(size_t)reg.d; 

		s->spans[i] = UploadSpan {
			.size = size,
			.token = req
		};

		req += size;
	}

	void *mapped = nullptr;
	// TODO: Use a persistently mapped ring buffer instead of creating new ones
	GLuint pbo = get_gl_staging_pbo(req, &mapped);

	if (!pbo)
		return UPLOAD_EUNKNOWN;

	for (size_t i = 0; i < s->span_count; ++i) {
		s->spans[i].ptr = (char*)mapped + s->spans[i].token; 
	}

	GLStagedImageUploader *uploader = new GLStagedImageUploader{};

	uploader->pbo = pbo;

	s->data = uploader;

	return UPLOAD_OK;
}

void end_staged_image_upload(UploadSession * s)
{
	GLStagedImageUploader *uploader = (GLStagedImageUploader*)s->data;
	GLImage *img = static_cast<GLImage*>(s->ent->data);

	GLenum format, type;
	img_format_to_gl(img->fmt, &format, &type);

	const bool is_3d = img->d != 0;

	glBindBuffer(GL_PIXEL_UNPACK_BUFFER,uploader->pbo);

	for (size_t i = 0; i < s->span_count; ++i) {
		UploadSpan sp = s->spans[i];
		ImageUploadRegion reg = s->regions.img[i];

		if (is_3d) {
			glTextureSubImage3D(
				img->id,
				reg.level,
				reg.x,
				reg.y,
				reg.z,
				reg.w,
				reg.h,
				reg.d,
				format,
				type,
				(void*)(sp.token)
			);
	 	} else {
			glTextureSubImage2D(
				img->id,
				reg.level,
				reg.x,
				reg.y,
				reg.w,
				reg.h,
				format,
				type,
				(void*)(sp.token)
			);
		}
	}

	glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);

	release_gl_staging_pbo(uploader->pbo);
	delete uploader;
}

void image_upload_end(
	UploadSession *s
)
{
	UploadMode mode = s->mode;

	if (mode == UPLOAD_MODE_MAPPED) {
		log_error("Completing image upload with invalid mode"); 
		return;
	}

	if (mode == UPLOAD_MODE_STAGING) { 
		end_staged_image_upload(s);
	}

	return;
}

//------------------------------------------------------------------------------
// Interface

struct UploadContext * get_upload_context(ResourceTable *rt)
{
	static UploadContext ctx;

	ctx.rt = rt;

	return &ctx;
}

extern UploadSession begin_buffer_upload(
	UploadContext *ctx,
	ResourceHandle h, 
	const UploadParams *params,
	const BufferUploadRegion *regions,
	uint32_t count
)
{
	UploadSession s;

	ResourceEntry *ent = ctx->rt->get_internal(h);

	if (!ent || ent->type != (uint32_t)RESOURCE_TYPE_BUFFER)
		return s;

	GLBuffer *buf = static_cast<GLBuffer*>(ent->data);
	UploadMode mode = params->mode;

	s.ent = ent;
	s.regions = {.buf = regions};
	s.mode = params->mode;

	switch (params->mode) {
		case UPLOAD_MODE_MAPPED:
			s.status = get_gl_buffer_mapped_span(&s.span, buf, params);
			break;
		case UPLOAD_MODE_STAGING:
			break;
		default:
			s.status = UPLOAD_EBADPARAM;
	}

	s.status = UPLOAD_OK;

	return s;
}

UploadSession begin_image_upload(
	UploadContext *ctx,
	ResourceHandle h, 
	const UploadParams *params,
	const ImageUploadRegion *regions,
	uint32_t count
)
{
	UploadSession s;

	ResourceEntry *ent = ctx->rt->get_internal(h);

	if (!ent || ent->type != (uint32_t)RESOURCE_TYPE_IMAGE)
		return s;

	UploadMode mode = params->mode;

	int res = UPLOAD_OK;

	s.ent = ent;
	s.spans = new UploadSpan[count]{};
	s.span_count = count;
	s.regions = {.img = regions};
	s.mode = mode;

	GLImage *img = static_cast<GLImage*>(ent->data);

	if (mode == UPLOAD_MODE_MAPPED) {
		s.status = UPLOAD_EBADPARAM;
		return s;
	}

	if (mode == UPLOAD_MODE_STAGING) { 
		s.status = init_staged_image_upload(&s, regions, img);
	}

	s.status = UPLOAD_OK;

	return s;
}

void upload_write_span(
	UploadSession *s, 
	uint32_t index,
	size_t offset,
	void *data, 
	size_t size
) 
{
	if (s->mode == UPLOAD_MODE_MAPPED) {
		memcpy((char*)s->span.ptr + offset, data, size);
		return;
	}
	UploadSpan span = s->spans[index];
	if (span.ptr) {
		memcpy((char*)span.ptr + offset, data, size);
		return;
	}
}

void end_upload(UploadSession *s)
{
	ResourceType type = (ResourceType)s->ent->type;

	if (type == RESOURCE_TYPE_BUFFER) {
		buffer_upload_end(s);
	}
	else if (type == RESOURCE_TYPE_IMAGE) {
		image_upload_end(s);
	}

	delete[] s->spans;
}
