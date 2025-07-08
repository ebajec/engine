#include "resource_loader.h"
#include "model_loader.h"
#include "gl_debug.h"

static LoadResult gl_model_create(ResourceLoader *loader, void **res, void *info);
static void gl_model_destroy(ResourceLoader *loader, void *model);

ResourceFns g_model_alloc_fns = {
	.create = gl_model_create,
	.destroy = gl_model_destroy,
	.load_file = nullptr
};

static LoadResult model_load_2d(ResourceLoader *loader, void *res, void *info);
static LoadResult model_load_3d(ResourceLoader *loader, void *res, void *info);

ResourceLoaderFns g_model_2d_load_fns {
	.loader_fn = model_load_2d,
	.post_load_fn = nullptr
};
ResourceLoaderFns g_model_3d_load_fns {
	.loader_fn = model_load_3d,
	.post_load_fn = nullptr
};

LoadResult model_load_2d(ResourceLoader *loader, void *res, void *info)
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

	return RESULT_SUCCESS;
}

LoadResult model_load_3d(ResourceLoader *loader, void *res, void *info)
{
	GLModel *model = static_cast<GLModel*>(res);
	Mesh3DCreateInfo *ci = static_cast<Mesh3DCreateInfo*>(info);

	model->type = MODEL_TYPE_MESH_3D;
	model->vcount = ci->vcount;
	model->vsize = sizeof(vertex3d);
	model->icount = ci->icount;
	model->isize = sizeof(uint32_t);

	glBindVertexArray(model->vao);

	glBindBuffer(GL_ARRAY_BUFFER,model->vbo);
	glBufferData(GL_ARRAY_BUFFER,(GLsizei)(model->vsize*model->vcount),ci->data,GL_DYNAMIC_DRAW);

	if (gl_check_err())
		goto failure;

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,model->ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizei)(model->icount*model->isize),ci->indices,GL_DYNAMIC_DRAW);

	if (gl_check_err())
		goto failure;

	glEnableVertexArrayAttrib(model->vao,0);
	glEnableVertexArrayAttrib(model->vao,1);
	glEnableVertexArrayAttrib(model->vao,2);
	glVertexAttribPointer(0,3,GL_FLOAT,0,sizeof(vertex3d),(void*)0);
	glVertexAttribPointer(1,2,GL_FLOAT,0,sizeof(vertex3d),(void*)offsetof(vertex3d,uv));
	glVertexAttribPointer(2,3,GL_FLOAT,0,sizeof(vertex3d),(void*)offsetof(vertex3d,normal));

	glBindVertexArray(0);

	if (gl_check_err())
		goto failure;

	return RESULT_SUCCESS;
failure:
	return RESULT_ERROR;
}
LoadResult gl_model_create(ResourceLoader *loader, void **res, void *info)
{
	std::unique_ptr<GLModel> model (new GLModel{});

	glGenVertexArrays(1,&model->vao);
	glGenBuffers(1,&model->vbo);
	glGenBuffers(1,&model->ibo);

	if (gl_check_err()) {
		return RESULT_ERROR;
	}

	*res = model.release();
	return RESULT_SUCCESS;
}

void gl_model_destroy(ResourceLoader *loader, void *res)
{
	if (!res)
		return;

	GLModel *model = static_cast<GLModel*>(res);

	if(model->vao) glDeleteVertexArrays(1,&model->vao);
	if(model->vbo) glDeleteBuffers(1,&model->vbo);
	if(model->ibo) glDeleteBuffers(1,&model->ibo);
}

ResourceHandle load_model_2d(ResourceLoader *loader, Mesh2DCreateInfo *ci)
{
	ResourceHandle h = loader->create_handle(RESOURCE_TYPE_MODEL);

	LoadResult result = loader->allocate(h, ci);

	if (result != RESULT_SUCCESS) 
		goto load_failed;

	result = loader->upload(h, RESOURCE_LOADER_MODEL_2D, ci);

	if (result != RESULT_SUCCESS) 
		goto load_failed;

	return h;

load_failed:
	loader->destroy_handle(h);
	return RESOURCE_HANDLE_NULL;
}

ResourceHandle load_model_3d(ResourceLoader *loader, Mesh3DCreateInfo *ci)
{
	ResourceHandle h = loader->create_handle(RESOURCE_TYPE_MODEL);

	LoadResult result = loader->allocate(h, ci);

	if (result != RESULT_SUCCESS) 
		goto load_failed;

	result = loader->upload(h, RESOURCE_LOADER_MODEL_3D, ci);

	if (result != RESULT_SUCCESS) 
		goto load_failed;

	return h;

load_failed:
	loader->destroy_handle(h);
	return RESOURCE_HANDLE_NULL;
}

const GLModel *get_model(ResourceLoader *loader, ResourceHandle h)
{
	const ResourceEntry *ent = loader->get(h);
	if (!ent || ent->type != RESOURCE_TYPE_MODEL)
		return nullptr;

	return static_cast<const GLModel*>(ent->data);
}
