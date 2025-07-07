#include "resource_loader.h"
#include "def_gl.h"

struct GLImage 
{
	int init();

	TexFormat fmt;
	gl_tex id;

	uint32_t w;
	uint32_t h;
	uint32_t d;
};

struct ImageFileCreateInfo
{
	std::string path;
};

typedef FileDesc TextureDesc;

extern ResourceFns g_image_loader_fns;

Handle load_image_file(ResourceLoader *loader, std::string_view path);
const GLImage *get_image(ResourceLoader *loader, Handle h);
