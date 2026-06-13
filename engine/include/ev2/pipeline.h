#ifndef EV2_PIPELINE_H
#define EV2_PIPELINE_H


#include "ev2/context.h"
#include "ev2/resource.h"

namespace ev2 {

MAKE_HANDLE(GfxPipeline);
MAKE_HANDLE(ComputePipeline);
MAKE_HANDLE(ShaderBindings);
MAKE_HANDLE(ShaderLayout);
MAKE_HANDLE(Shader);
MAKE_HANDLE(Recorder);

MAKE_HANDLE(DescriptorSet);

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

ShaderID load_shader(GfxContext *ctx, const char *path);
void unload_shader(GfxContext *ctx, ShaderID id);

GfxPipelineID load_graphics_pipeline(GfxContext *ctx, const char *path);
void unload_graphics_pipeline(GfxContext *ctx, GfxPipelineID pipe);

ComputePipelineID load_compute_pipeline(GfxContext *ctx, const char *path);
void unload_compute_pipeline(GfxContext *ctx, ComputePipelineID pipe);

//------------------------------------------------------------------------------
// Bindings

ShaderBindingsID create_bindings(GfxContext *ctx, 
								GfxPipelineID pipeline_id, uint32_t index);

ShaderBindingsID create_bindings(GfxContext *ctx, 
								ComputePipelineID pipeline_id, uint32_t index);

ev2::Result bind_buffer(
	GfxContext *ctx, 
	ShaderBindingsID binding_handle, 
	BufferID buffer_handle, 
	const char *name,
	size_t offset, 
	size_t size
);

ev2::Result bind_texture(
	GfxContext *ctx, 
	ShaderBindingsID binding_handle,
	TextureID texture_handle,  
	const char *name
); 

ev2::Result bind_image(
	GfxContext *ctx,
	ShaderBindingsID binding_handle,
	ImageID image_handle,
	const char *name 
);

ev2::Result flush_bindings(GfxContext *ctx, ShaderBindingsID bindings_id);

//------------------------------------------------------------------------------
// rendering

MAKE_HANDLE(View);
MAKE_HANDLE(Pass);
MAKE_HANDLE(RenderTarget);

struct DrawCommand 
{
    unsigned int  count;
    unsigned int  instanceCount;
    unsigned int  firstIndex;
    int  baseVertex;
    unsigned int  baseInstance;
};

enum RenderTargetFlagBits
{
	// Create a new color image for this target 
	RENDER_TARGET_CREATE_COLOR_BIT = 0x2,
	// Create a new depth buffer for this target
	RENDER_TARGET_CREATE_DEPTH_BIT = 0x4,
};
typedef uint32_t RenderTargetFlags;

RenderTargetID create_render_target(
	GfxContext *ctx, 
	uint32_t w, 
	uint32_t h, 
	RenderTargetFlags flags
);
void destroy_render_target(
	GfxContext *ctx, 
	RenderTargetID id
);

ev2::Result begin_frame(GfxContext *ctx);
void end_frame(GfxContext *ctx);


ViewID create_view(GfxContext *ctx, float view[], float proj[]);
void update_view(GfxContext *ctx, ViewID handle, float view[], float proj[]);
void destroy_view(GfxContext *ctx, ViewID handle);

struct PassContext
{
	RecorderID rec;
	PassID pass;
};

struct Rect
{
	uint32_t x0, y0;
	uint32_t w, h;
};

enum EndPassFlagBits
{
	END_PASS_CONTINUE_COMMANDS
};
typedef uint32_t EndPassFlags;

struct BeginPassInfo
{
	uint32_t buffer_count;
	const BufferID *buffers;

	uint32_t image_count;
	const ImageID *images;
};

// @brief Begin a render pass.  Configures bindings for pass-specific data  
// (view matrices, etc).    
PassID begin_pass(
	GfxContext *ctx, 
	RenderTargetID target, ViewID view,
	Rect viewport, Rect scissor = {}
);

// @brief Begin a render pass.  Configures bindings for pass-specific data  
// (view matrices, etc).    
PassID begin_pass(
	GfxContext *ctx 
);

SyncID end_pass(GfxContext *ctx, PassID pass);

//------------------------------------------------------------------------------
// command recording

enum CommandMode {
	MODE_PRIMARY,
	MODE_SECONDARY
};

enum DrawMode
{
	MODE_TRIANGLES
};

void cmd_bind_resources(PassID pass_id, ShaderBindingsID bindings_id);

void cmd_bind_compute_pipeline(PassID pass_id, ComputePipelineID pipeline_id);
void cmd_bind_gfx_pipeline(PassID pass_id, GfxPipelineID pipeline_id);

void cmd_bind_index_buffer(PassID pass_id, BufferID buf);
void cmd_draw_screen_quad(PassID pass_id); 

void cmd_dispatch(
	PassID pass_id,
	uint32_t countx, 
	uint32_t county, 
	uint32_t countz
);

void cmd_use_buffer(
	PassID pass_id,
	BufferID buf_id,
	Usage usage
);

void cmd_use_image(
	PassID pass_id,
	ImageID img_id,
	Usage usage
);
};

#endif //EV2_PIPELINE_H
