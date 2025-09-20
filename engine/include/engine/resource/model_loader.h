#ifndef MODEL_LOADER_H
#define MODEL_LOADER_H

#include "engine/resource/resource_table.h"
#include "engine/renderer/opengl.h"

struct GLModel
{
	int init();
	ModelType type;

	GLuint vao;

	size_t vcount;
	size_t icount;

	GLuint vbo;
	GLuint ibo;
	size_t vsize;

	size_t isize;
};

extern ResourceAllocFns g_model_alloc_fns;

const GLModel *get_model(ResourceTable *loader, ResourceHandle h);

typedef ResourceHandle ModelID;

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


class ModelLoader
{

public:
	static void registration(ResourceTable *loader);

	static ResourceHandle load_2d(ResourceTable *loader, Mesh2DCreateInfo *ci);
	static ResourceHandle load_3d(ResourceTable *loader, Mesh3DCreateInfo *ci);
};

extern ResourceHandle model_create(ResourceTable *loader);
extern ResourceHandle model_upload(ResourceTable *loader);

#endif // MODEL_LOADER_H
