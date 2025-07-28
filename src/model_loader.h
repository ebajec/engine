#ifndef MODEL_LOADER_H
#define MODEL_LOADER_H

#include "resource_loader.h"

struct GLModel
{
	int init();
	ModelType type;

	gl_vao vao;

	size_t vcount;
	size_t icount;

	gl_vbo vbo;
	gl_vbo ibo;
	size_t vsize;

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

extern ResourceAllocFns g_model_alloc_fns;

class ModelLoader
{

public:
	static void registration(ResourceLoader *loader);

	static ResourceHandle load_2d(ResourceLoader *loader, Mesh2DCreateInfo *ci);
	static ResourceHandle load_3d(ResourceLoader *loader, Mesh3DCreateInfo *ci);
};

extern ResourceHandle model_create(ResourceLoader *loader);
extern ResourceHandle model_upload(ResourceLoader *loader);

const GLModel *get_model(ResourceLoader *loader, ResourceHandle h);

#endif // MODEL_LOADER_H
