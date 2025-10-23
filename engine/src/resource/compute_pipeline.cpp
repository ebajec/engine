#include <engine/resource/compute_pipeline.h>
#include <engine/resource/resource_table.h>

#include <resource/gl_utils.h>

// OpenGL impl

static LoadResult gl_compute_pipeline_create(ResourceTable *rt, void **res, void *info)
{
	const char *path = static_cast<const char *>(info);

	ShaderID shaderID = shader_load_file(rt, path);

	if (!shaderID)
		return RT_EUNKNOWN;


	const GLShaderModule *comp = get_shader(rt, shaderID);

	if (comp->stage != GL_COMPUTE_SHADER) {
		return RT_EUNKNOWN;
	}

	GLuint program = glCreateProgram();
	glAttachShader(program, comp->id);
	glLinkProgram(program);

	if (!gl_check_program(program, path)) {
		glDeleteProgram(program);
		return RT_EUNKNOWN;
	}

	GLComputePipeline *pipeline = new GLComputePipeline{};
	pipeline->shader = shaderID;
	pipeline->program = program;
	*res = pipeline;

	return RT_OK;
}

static void gl_compute_pipeline_destroy(ResourceTable *rt, void *res)
{
	GLComputePipeline *pipeline = static_cast<GLComputePipeline*>(res);

	glDeleteProgram(pipeline->program);
	delete pipeline;
}

static LoadResult gl_compute_pipeline_load_file(ResourceTable *rt, ResourceHandle h, const char *path)
{
	LoadResult result = rt->allocate(h,(void*)path);

	if (result != RT_OK) {
		return result;
	}

	return result;
}


ResourceAllocFns gl_compute_pipeline_alloc_fns = {
	.create = gl_compute_pipeline_create,
	.destroy = gl_compute_pipeline_destroy,
	.load_file = gl_compute_pipeline_load_file
};

ComputePipelineID compute_pipeline_load(ResourceTable *rt, std::string_view path) 
{
	if (ResourceHandle h = rt->find(path)) 
		return h;

	ResourceHandle h = rt->create_handle(RESOURCE_TYPE_COMPUTE_PIPELINE);

	LoadResult result = rt->load_file(h,path.data());

	if (result != RT_OK)
		goto error_cleanup;

	rt->set_handle_key(h,path);
	return h;

error_cleanup:
	log_error("Failed to load material file at %s",path.data());
	rt->destroy_handle(h);
	return RESOURCE_HANDLE_NULL;
}
