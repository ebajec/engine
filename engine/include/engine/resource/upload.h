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
	UPLOAD_MODE_STAGING
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
	void *data;

	union {
		struct {
			UploadSpan *spans;
			uint32_t span_count;
		};
		UploadSpan span;
	};

	union {
		const BufferUploadRegion *buf;
		const ImageUploadRegion *img;
	} regions;

	UploadMode mode;

	UploadResult status = UPLOAD_ENULL;
};

struct UploadContext;
struct UploadContext * get_upload_context(ResourceTable *rt);

extern UploadSession begin_buffer_upload(
	UploadContext *s,
	ResourceHandle h, 
	const UploadParams *params,
	const BufferUploadRegion *regions = nullptr,
	uint32_t count = 0
);

extern UploadSession begin_image_upload(
	UploadContext *s,
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

extern void end_upload(UploadSession *ctx);

#endif // RESOURCE_UPLOAD_H

