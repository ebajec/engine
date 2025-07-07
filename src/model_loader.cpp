#include "resource_loader.h"
#include "model_loader.h"
#include "gl_debug.h"

static LoadResult gl_model_create(ResourceLoader *loader, void **res, void *info);
static void gl_model_destroy(ResourceLoader *loader, void *model);

ResourceFns g_model_loader_fns = {
	.create_fn = gl_model_create,
	.destroy_fn = gl_model_destroy,
};

int GLModel::init()
{
	glGenVertexArrays(1,&vao);
	glGenBuffers(1,&vbo);
	glGenBuffers(1,&ibo);

	return gl_check_err();
}

LoadResult model_load_2d(GLModel* model, const Mesh2DCreateInfo *info)
{
	model->type = MODEL_TYPE_MESH_2D;
	model->vcount = info->vcount;
	model->vsize = sizeof(vertex2d);
	model->icount = info->icount;
	model->isize = sizeof(uint32_t);

	glBindVertexArray(model->vao);

	glBindBuffer(GL_ARRAY_BUFFER,model->vbo);
	glBufferData(GL_ARRAY_BUFFER,model->vsize*model->vcount,info->data,GL_DYNAMIC_DRAW);

	gl_check_err();

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,model->ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,model->icount*model->isize,info->indices,GL_DYNAMIC_DRAW);

	gl_check_err();

	glEnableVertexArrayAttrib(model->vao,0);
	glEnableVertexArrayAttrib(model->vao,1);
	glVertexAttribPointer(0,2,GL_FLOAT,0,sizeof(vertex2d),(void*)0);
	glVertexAttribPointer(1,2,GL_FLOAT,0,sizeof(vertex2d),(void*)offsetof(vertex2d,uv));

	glBindVertexArray(0);

	gl_check_err();

	return RESULT_SUCCESS;
}

LoadResult model_load_3d(GLModel *model, const Mesh3DCreateInfo *info)
{
	model->type = MODEL_TYPE_MESH_3D;
	model->vcount = info->vcount;
	model->vsize = sizeof(vertex3d);
	model->icount = info->icount;
	model->isize = sizeof(uint32_t);

	glBindVertexArray(model->vao);

	glBindBuffer(GL_ARRAY_BUFFER,model->vbo);
	glBufferData(GL_ARRAY_BUFFER,model->vsize*model->vcount,info->data,GL_DYNAMIC_DRAW);

	gl_check_err();

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,model->ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,model->icount*model->isize,info->indices,GL_DYNAMIC_DRAW);

	gl_check_err();

	glEnableVertexArrayAttrib(model->vao,0);
	glEnableVertexArrayAttrib(model->vao,1);
	glEnableVertexArrayAttrib(model->vao,2);
	glVertexAttribPointer(0,3,GL_FLOAT,0,sizeof(vertex3d),(void*)0);
	glVertexAttribPointer(1,2,GL_FLOAT,0,sizeof(vertex3d),(void*)offsetof(vertex3d,uv));
	glVertexAttribPointer(2,3,GL_FLOAT,0,sizeof(vertex3d),(void*)offsetof(vertex3d,normal));

	glBindVertexArray(0);

	gl_check_err();

	return RESULT_SUCCESS;
}

LoadResult gl_model_create(ResourceLoader *loader, void **res, void *info)
{
	ModelDesc desc = *static_cast<ModelDesc*>(info);
	std::unique_ptr<GLModel> model (new GLModel{});

	model->init();

	LoadResult result = RESULT_SUCCESS;

	if (std::holds_alternative<Mesh2DCreateInfo*>(desc)) {
		result = model_load_2d(model.get(),std::get<Mesh2DCreateInfo*>(desc));
	} else if (std::holds_alternative<Mesh3DCreateInfo*>(desc)) {
		result = model_load_3d(model.get(),std::get<Mesh3DCreateInfo*>(desc));
	}

	*res = model.release();

	return result;
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

LoadResult load_model(ResourceLoader *loader, Handle h, ModelDesc desc)
{
	return resource_load(loader, h, &desc);
}

const GLModel *get_model(ResourceLoader *loader, Handle h)
{
	const ResourceEntry *ent = loader->get(h);
	if (!ent || ent->type != RESOURCE_TYPE_MODEL)
		return nullptr;

	return static_cast<const GLModel*>(ent->data);
}
