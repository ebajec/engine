#include "resource_loader.h"

#include <variant>

// Renderable geometry
struct GLModel
{
	int init();
	ModelType type;

	gl_vao vao;
	gl_vbo vbo;
	gl_vbo ibo;
	size_t vcount;
	size_t vsize;

	size_t icount;
	size_t isize;
};

struct Mesh2DCreateInfo
{
	const vertex2d* data; 
	size_t vcount; 

	const uint32_t* indices; 
	size_t icount;
};

struct Mesh3DCreateInfo
{
	const vertex3d* data; 
	size_t vcount; 

	const uint32_t* indices; 
	size_t icount;
};

typedef std::variant<Mesh3DCreateInfo*, Mesh2DCreateInfo*> ModelDesc;

extern ResourceFns g_model_alloc_fns;
extern ResourceLoaderFns g_model_2d_load_fns;
extern ResourceLoaderFns g_model_3d_load_fns;

extern ResourceHandle load_model_2d(ResourceLoader *loader, Mesh2DCreateInfo *ci);
extern ResourceHandle load_model_3d(ResourceLoader *loader, Mesh3DCreateInfo *ci);

const GLModel *get_model(ResourceLoader *loader, ResourceHandle h);




