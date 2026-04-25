#ifndef EV2_PIPELINE_H
#define EV2_PIPELINE_H

#include "ev2/context.h"
#include "ev2/resource.h"

namespace ev2 {

MAKE_HANDLE(GraphicsPipeline);
MAKE_HANDLE(ComputePipeline);
MAKE_HANDLE(DescriptorSet);
MAKE_HANDLE(DescriptorLayout);
MAKE_HANDLE(Shader);
MAKE_HANDLE(Recorder);
MAKE_HANDLE(Sync);

enum ShaderStage
{
	STAGE_VERTEX,
	STAGE_FRAGMENT,
	STAGE_COMPUTE,
	STAGE_MAX_ENUM,
};

enum Usage
{
    USAGE_UNDEFINED,
    USAGE_TRANSFER_SRC,
    USAGE_TRANSFER_DST,
    USAGE_SAMPLED_GRAPHICS,
    USAGE_COLOR_ATTACHMENT,
    USAGE_DEPTH_ATTACHMENT,
    USAGE_STORAGE_READ_COMPUTE,
    USAGE_STORAGE_RW_COMPUTE,
    USAGE_MAX_ENUM,
};

struct BindingSlot
{
	uint32_t set;
	uint32_t id;
};

ShaderID load_shader(Context *dev, const char *path);
void unload_shader(Context *dev, ShaderID id);

GraphicsPipelineID load_graphics_pipeline(Context *dev, const char *path);
void unload_graphics_pipeline(Context *dev, GraphicsPipelineID pipe);

ComputePipelineID load_compute_pipeline(Context *dev, const char *path);
void unload_compute_pipeline(Context *dev, ComputePipelineID pipe);

DescriptorLayoutID get_graphics_pipeline_layout(
	Context *Dev, GraphicsPipelineID pipe);
DescriptorLayoutID get_compute_pipeline_layout(
	Context *Dev, ComputePipelineID pipe);

BindingSlot find_binding(DescriptorLayoutID layout, const char *name);

DescriptorSetID create_descriptor_set(
	Context *dev, 
	DescriptorLayoutID layout,
	uint16_t index = 0
);

void destroy_descriptor_set(
	Context *dev, 
	DescriptorSetID set 
);

ev2::Result bind_buffer(
	Context *dev, 
	DescriptorSetID set, 
	BindingSlot slot, 
	BufferID buf, 
	size_t offset, 
	size_t size
); 

ev2::Result bind_texture(
	Context *dev, 
	DescriptorSetID set, 
	BindingSlot slot, 
	TextureID tex 
); 

ev2::Result bind_image(
	Context *dev,
	DescriptorSetID set, 
	BindingSlot slot, 
	ImageID img
);

// command recording

enum CommandMode {
	MODE_PRIMARY,
	MODE_SECONDARY
};

RecorderID begin_commands(Context * dev, CommandMode mode = MODE_PRIMARY);
SyncID end_commands(RecorderID recorder);

void submit(SyncID);

void cmd_bind_descriptor_set(RecorderID rec, DescriptorSetID set_id);

void cmd_bind_compute_pipeline(RecorderID rec, ComputePipelineID h);

void cmd_dispatch(
	RecorderID rec,
	uint32_t countx, 
	uint32_t county, 
	uint32_t countz
);

void cmd_use_buffer(
	RecorderID rec,
	BufferID buf,
	Usage usage
);

void cmd_use_texture(
	RecorderID rec,
	TextureID tex,
	Usage usage
);

};

#endif //EV2_PIPELINE_H
