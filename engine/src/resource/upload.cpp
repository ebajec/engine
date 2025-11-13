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

int get_gl_buffer_mapped_span(UploadSpan *span, GLBuffer *buf, const UploadParams *params)
{
	GLint map_align = 0;
	glGetIntegerv(GL_MIN_MAP_BUFFER_ALIGNMENT, &map_align);

	void *ptr = glMapNamedBuffer(
			buf->id, 
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

GLuint get_gl_staging_pbo(size_t size)
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

int init_staged_image_upload(
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
		req += size;

		s->spans[i] = UploadSpan {
			.size = size,
			.token = req
		};
	}

	// TODO: Use a persistently mapped ring buffer instead of creating new ones
	GLuint pbo = get_gl_staging_pbo(req);

	if (!pbo)
		return UPLOAD_EUNKNOWN;

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

extern int begin_buffer_upload(
	UploadContext *ctx,
	UploadSession *s,
	ResourceHandle h, 
	const UploadParams *params,
	const BufferUploadRegion *regions,
	uint32_t count
)
{
	ResourceEntry *ent = ctx->rt->get_internal(h);

	if (!ent || ent->type != RESOURCE_TYPE_BUFFER)
		return UPLOAD_ENULL;

	GLBuffer *buf = static_cast<GLBuffer*>(ent->data);
	UploadMode mode = params->mode;

	int res = UPLOAD_OK;

	*s = {
		.ent = ent,
		.regions = {.buf = regions},
		.mode = params->mode,
	};

	switch (params->mode) {
		case UPLOAD_MODE_MAPPED:
			res = get_gl_buffer_mapped_span(&s->spans[0], buf, params);
			break;
		case UPLOAD_MODE_STAGING:
			break;
		default:
			res = UPLOAD_EBADPARAM;
	}

	return res;
}

int begin_image_upload(
	UploadContext *ctx,
	UploadSession *s,
	ResourceHandle h, 
	const UploadParams *params,
	const ImageUploadRegion *regions,
	uint32_t count
)
{
	ResourceEntry *ent = ctx->rt->get_internal(h);

	if (!ent || ent->type != RESOURCE_TYPE_IMAGE)
		return UPLOAD_ENULL;

	UploadMode mode = params->mode;

	int res = UPLOAD_OK;

	*s = {
		.ent = ent,
		.spans = new UploadSpan[count]{},
		.span_count = count,
		.regions = {.img = regions},
	};

	GLImage *img = static_cast<GLImage*>(ent->data);

	if (mode == UPLOAD_MODE_MAPPED)
		return UPLOAD_EBADPARAM;


	if (mode == UPLOAD_MODE_STAGING) { 
		return init_staged_image_upload(s, regions, img);
	}

	return res;
}

void upload_write_span(
	UploadSession *s, 
	uint32_t index,
	size_t offset,
	void *data, 
	size_t size
) 
{
	UploadSpan span = s->spans[index];
	if (span.ptr) {
		memcpy((char*)span.ptr + offset, data, size);
		return;
	}
}

void upload_end(UploadSession *s)
{
	ResourceType type = s->ent->type;

	if (type == RESOURCE_TYPE_BUFFER) {
		buffer_upload_end(s);
	}
	else if (type == RESOURCE_TYPE_IMAGE) {
		image_upload_end(s);
	}

	delete[] s->spans;
}
