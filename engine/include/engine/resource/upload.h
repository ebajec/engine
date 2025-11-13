#ifndef RESOURCE_UPLOAD_H
#define RESOURCE_UPLOAD_H

#include "engine/resource/resource_table.h"

#include <cstddef>

enum UploadResult
{
	UPLOAD_OK,
	UPLOAD_ETOOBIG,
	UPLOAD_EBADTYPE,
	UPLOAD_EBADPARAM,
	UPLOAD_EUNKNOWN,
	UPLOAD_ENULL,
};

enum UploadMode
{
	UPLOAD_MODE_MAPPED,
	UPLOAD_MODE_STAGING,
	UPLOAD_MODE_DIRECT
};

struct UploadParams
{
	UploadMode mode;
};

struct UploadSpan
{
	void *ptr; // mapped pointer if available
	size_t size;	
	size_t align;
	uint64_t token; // backend handle
};

struct ImageUploadRegion
{
	uint32_t x, y, z;
	uint32_t w, h, d{1};
	uint32_t level;
};

struct BufferUploadRegion
{
	size_t offset;
	size_t size;
};

struct UploadSession
{
	ResourceEntry *ent;

	UploadSpan *spans;
	uint32_t span_count;

	union {
		const BufferUploadRegion *buf;
		const ImageUploadRegion *img;
	} regions;

	UploadMode mode;

	void *data;
};

struct UploadContext;

extern int begin_buffer_upload(
	UploadContext *ctx,
	UploadSession *s,
	ResourceHandle h, 
	const UploadParams *params,
	const BufferUploadRegion *regions = nullptr,
	uint32_t count = 0
);

extern int begin_image_upload(
	UploadContext *ctx,
	UploadSession *s,
	ResourceHandle h, 
	const UploadParams *params,
	const ImageUploadRegion *regions = nullptr,
	uint32_t count = 0
);

extern void upload_write_span(
	UploadSession *s, 
	uint32_t index,
	size_t offset,
	void *data, 
	size_t size
);

extern void upload_end(UploadSession *ctx);

#endif // RESOURCE_UPLOAD_H

