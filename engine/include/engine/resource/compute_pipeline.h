#ifndef COMPUTE_PIPELINE_H
#define COMPUTE_PIPELINE_H

#include <engine/renderer/opengl.h>
#include <engine/resource/resource_table.h>
#include <engine/resource/shader_loader.h>

extern ResourceAllocFns gl_compute_pipeline_alloc_fns;

struct GLComputePipeline
{
	ShaderID shader;
	GLuint program;
};

typedef ResourceHandle ComputePipelineID;

ComputePipelineID compute_pipeline_load(ResourceTable *rt, std::string_view path);

#endif // COMPUTE_PIPELINE_H
