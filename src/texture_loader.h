#include "resource_loader.h"
#include "def_gl.h"

struct GLImage 
{
	gl_tex id;

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
	static void registration(ResourceLoader *loader);
};

ResourceHandle create_image_2d(ResourceLoader *loader, uint32_t w, uint32_t h, 
							   TexFormat fmt = TEX_FORMAT_RGBA8);
ResourceHandle load_image_file(ResourceLoader *loader, std::string_view path);

const GLImage *get_image(ResourceLoader *loader, ResourceHandle h);
