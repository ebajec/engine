#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include "resource_table.h"
#include "renderer/types.h"

#include "renderer/opengl.h"
struct GLImage 
{
	GLuint id;

	uint32_t w;
	uint32_t h;
	uint32_t d;

	TexFormat fmt;
};

extern ResourceAllocFns g_image_alloc_fns;

struct ImageCreateInfo
{
	uint32_t w;
	uint32_t h;
	TexFormat fmt;
};

class ImageLoader
{
public:
	static constexpr char const * name = "image";
	static void registration(ResourceTable *table);
};

ResourceHandle create_image_2d(ResourceTable *table, uint32_t w, uint32_t h, 
							   TexFormat fmt = TEX_FORMAT_RGBA8);
ResourceHandle load_image_file(ResourceTable *table, std::string_view path);

const GLImage *get_image(ResourceTable *table, ResourceHandle h);

#endif //IMAGE_LOADER_H
