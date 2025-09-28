#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include "engine/renderer/types.h"
#include "engine/renderer/opengl.h"
#include "engine/resource/resource_table.h"

extern ResourceAllocFns gl_image_alloc_fns;

struct GLImage 
{
	GLuint id;

	uint32_t w;
	uint32_t h;
	uint32_t d;

	ImgFormat fmt;
};

struct ImageCreateInfo
{
	uint32_t w;
	uint32_t h;
	ImgFormat fmt;
};

const GLImage *get_image(ResourceTable *table, ResourceHandle h);

typedef ResourceHandle ImageID;

class ImageLoader
{
public:
	static constexpr char const * name = "image";
	static void registration(ResourceTable *table);
};

ResourceHandle image_create_2d(ResourceTable *table, uint32_t w, uint32_t h, 
							   ImgFormat fmt = IMG_FORMAT_RGBA8);
ResourceHandle image_load_file(ResourceTable *table, std::string_view path);

#endif //IMAGE_LOADER_H
