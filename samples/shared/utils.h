#ifndef EV2_SAMPLES_UTILS_H
#define EV2_SAMPLES_UTILS_H

#include <cstdint>
#include <cstring>
#include <ev2/resource.h>

template<typename T>
static uint64_t initialize_image(ev2::GfxContext *ctx, ev2::ImageID img, T defval = (T)0)
{
	uint32_t w, h, d;
	ev2::get_image_dims(ctx, img, &w, &h, &d);

	size_t size = w * h * sizeof(T);
	ev2::UploadContext uc = ev2::begin_upload(ctx, size, alignof(T));

	for (size_t i = 0; i < size; i += sizeof(T)) {
		memcpy((char*)uc.ptr + i, &defval, sizeof(T)); 
	}
	ev2::ImageUpload upload = {
		.src_offset = 0,
		.x = 0, 
		.y = 0,
		.w = w,
		.h = h,
	};
	return ev2::commit_image_uploads(ctx, uc, img, &upload, 1);
}
#endif
