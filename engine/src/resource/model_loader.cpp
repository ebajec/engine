#include "renderer/opengl.h"
#include "renderer/gl_debug.h"

#include "resource/resource_table.h"
#include "resource/model_loader.h"

static LoadResult gl_model_create(ResourceTable *table, void **res, void *info);
static void gl_model_destroy(ResourceTable *table, void *model);

ResourceAllocFns gl_model_alloc_fns = {
	.create = gl_model_create,
	.destroy = gl_model_destroy,
	.load_file = nullptr
};

static LoadResult gl_model_load_2d(ResourceTable *table, void *res, void *info);
static LoadResult gl_model_load_3d(ResourceTable *table, void *res, void *info);

ResourceLoaderFns g_model_2d_load_fns {
	.loader_fn = gl_model_load_2d,
	.post_load_fn = nullptr
};
ResourceLoaderFns g_model_3d_load_fns {
	.loader_fn = gl_model_load_3d,
	.post_load_fn = nullptr
};

LoadResult gl_model_load_2d(ResourceTable *table, void *res, void *info)
{
	GLModel *model = static_cast<GLModel*>(res);
	Mesh2DCreateInfo *ci = static_cast<Mesh2DCreateInfo*>(info);

	model->type = MODEL_TYPE_MESH_2D;
	model->vcount = ci->vcount;
	model->vsize = sizeof(vertex2d);
	model->icount = ci->icount;
	model->isize = sizeof(uint32_t);

	glBindVertexArray(model->vao);

	glBindBuffer(GL_ARRAY_BUFFER,model->vbo);
	glBufferData(GL_ARRAY_BUFFER,(GLsizei)(model->vsize*(size_t)model->vcount),ci->data,GL_DYNAMIC_DRAW);

	gl_check_err();

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,model->ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizei)(model->icount*(size_t)model->isize),ci->indices,GL_DYNAMIC_DRAW);

	gl_check_err();

	glEnableVertexArrayAttrib(model->vao,0);
	glEnableVertexArrayAttrib(model->vao,1);
	glVertexAttribPointer(0,2,GL_FLOAT,0,sizeof(vertex2d),(void*)0);
	glVertexAttribPointer(1,2,GL_FLOAT,0,sizeof(vertex2d),(void*)offsetof(vertex2d,uv));

	glBindVertexArray(0);

	gl_check_err();

	return RT_OK;
}

LoadResult gl_model_load_3d(ResourceTable *table, void *res, void *info)
{
	GLModel *model = static_cast<GLModel*>(res);
	Mesh3DCreateInfo *ci = static_cast<Mesh3DCreateInfo*>(info);

	model->type = MODEL_TYPE_MESH_3D;
	model->vcount = ci->vcount;
	model->vsize = sizeof(vertex3d);
	model->icount = ci->icount;
	model->isize = sizeof(uint32_t);


	glBindVertexArray(model->vao);

	size_t vsize = (model->vsize*model->vcount);
	size_t isize = (model->icount*model->isize);

	glBindBuffer(GL_ARRAY_BUFFER,model->vbo);
	glBufferData(GL_ARRAY_BUFFER,(GLsizei)vsize,ci->data,GL_DYNAMIC_DRAW);

	if (gl_check_err())
		goto failure;


	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,model->ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizei)isize,ci->indices,GL_DYNAMIC_DRAW);

	if (gl_check_err())
		goto failure;

	glEnableVertexArrayAttrib(model->vao,0);
	glEnableVertexArrayAttrib(model->vao,1);
	glEnableVertexArrayAttrib(model->vao,2);
	glVertexAttribPointer(0,3,GL_FLOAT,0,sizeof(vertex3d),(void*)offsetof(vertex3d,position));
	glVertexAttribPointer(1,2,GL_FLOAT,0,sizeof(vertex3d),(void*)offsetof(vertex3d,uv));
	glVertexAttribPointer(2,3,GL_FLOAT,0,sizeof(vertex3d),(void*)offsetof(vertex3d,normal));

	glBindVertexArray(0);

	if (gl_check_err())
		goto failure;

	return RT_OK;
failure:
	return RT_EUNKNOWN;
}
LoadResult gl_model_create(ResourceTable *table, void **res, void *info)
{
	std::unique_ptr<GLModel> model (new GLModel{});

	glGenVertexArrays(1,&model->vao);
	glGenBuffers(1,&model->vbo);
	glGenBuffers(1,&model->ibo);

	if (gl_check_err()) {
		return RT_EUNKNOWN;
	}

	*res = model.release();
	return RT_OK;
}

void gl_model_destroy(ResourceTable *table, void *res)
{
	if (!res)
		return;

	GLModel *model = static_cast<GLModel*>(res);

	if(model->vao) glDeleteVertexArrays(1,&model->vao);
	if(model->vbo) glDeleteBuffers(1,&model->vbo);
	if(model->ibo) glDeleteBuffers(1,&model->ibo);

	delete model;
}

void ModelLoader::registration(ResourceTable *table)
{
	table->register_loader("model2d", g_model_2d_load_fns);
	table->register_loader("model3d", g_model_3d_load_fns);
}

ResourceHandle ModelLoader::model_load_2d(ResourceTable *table, Mesh2DCreateInfo *ci)
{
	ResourceHandle h = table->create_handle(RESOURCE_TYPE_MODEL);

	LoadResult result = table->allocate(h, ci);

	if (result != RT_OK) 
		goto load_failed;

	result = table->upload(h, "model2d", ci);

	if (result != RT_OK) 
		goto load_failed;

	return h;

load_failed:
	table->destroy_handle(h);
	return RESOURCE_HANDLE_NULL;
}

ResourceHandle ModelLoader::model_load_3d(ResourceTable *table, Mesh3DCreateInfo *ci)
{
	ResourceHandle h = table->create_handle(RESOURCE_TYPE_MODEL);

	LoadResult result = table->allocate(h, ci);

	if (result != RT_OK) 
		goto load_failed;

	result = table->upload(h, "model3d", ci);

	if (result != RT_OK) 
		goto load_failed;

	return h;

load_failed:
	table->destroy_handle(h);
	return RESOURCE_HANDLE_NULL;
}

extern ResourceHandle model_create(ResourceTable *table)
{
	ResourceHandle h = table->create_handle(RESOURCE_TYPE_MODEL);
	LoadResult result = table->allocate(h, nullptr);

	if (result != RT_OK) 
		goto load_failed;

	return h;

load_failed:
	table->destroy_handle(h);
	return RESOURCE_HANDLE_NULL;
}

const GLModel *get_model(ResourceTable *table, ResourceHandle h)
{
	const ResourceEntry *ent = table->get(h);
	if (!ent || ent->type != RESOURCE_TYPE_MODEL)
		return nullptr;

	return static_cast<const GLModel*>(ent->data);
}
