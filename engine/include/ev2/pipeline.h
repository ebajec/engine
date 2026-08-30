#ifndef EV2_PIPELINE_H
#define EV2_PIPELINE_H

#include "ev2/defines.h"
#include "ev2/context.h"
#include "ev2/resource.h"
#include "ev2/asset.h"

#include <functional>

constexpr uint32_t EV2_MAX_BINDLESS_DESCRIPTORS = 1024;

MAKE_HANDLE_VERSIONED(Bindings);

namespace ev2 {

enum ShaderStage
{
	STAGE_VERTEX,
	STAGE_FRAGMENT,
	STAGE_COMPUTE,
};

enum Usage
{
    USAGE_UNDEFINED,
    USAGE_TRANSFER_SRC,
    USAGE_TRANSFER_DST,
    USAGE_SAMPLED_GRAPHICS,
    USAGE_UNIFORM_READ,
    USAGE_COLOR_ATTACHMENT,
    USAGE_DEPTH_ATTACHMENT,
    USAGE_SAMPLED_COMPUTE,
    USAGE_STORAGE_READ_GRAPHICS,
    USAGE_STORAGE_READ_COMPUTE,
    USAGE_STORAGE_WRITE_COMPUTE,
    USAGE_STORAGE_READ_WRITE_COMPUTE,
    USAGE_INDEX_INPUT,
    USAGE_VERTEX_INPUT,
    USAGE_MAX_ENUM,
};

enum BindingMode {
	BINDING_MODE_STATIC,
	BINDING_MODE_DYNAMIC
};

BindingsID create_bindings(GfxContext *ctx, GfxPipelineID pipeline_id, 
								 uint32_t index, BindingMode mode);
BindingsID create_bindings(GfxContext *ctx, ComputePipelineID pipeline_id, 
								 uint32_t index, BindingMode mode);

void destroy_bindings(GfxContext *ctx, BindingsID bindings);

ev2::Result reset_bindings(GfxContext *ctx, BindingsID bindings);
void flush_bindings(GfxContext *ctx, BindingsID bindings_id);

/// @brief Bind a buffer to the binding given by name.
/// The binding is assumed to not be an array type.
ev2::Result bind_buffer(
	GfxContext *ctx, 
	BindingsID binding_handle, 
	const char *name,
	BufferID buffer_handle, 
	size_t offset, 
	size_t size
);

/// @brief Bind a texture to the binding given by name.
/// The binding is assumed to not be an array type.
ev2::Result bind_texture(
	GfxContext *ctx, 
	BindingsID binding_handle,
	const char *name,
	TextureID texture_handle  
); 

ev2::Result bind_texture_indexed(
	GfxContext *ctx, 
	BindingsID binding_handle,
	const char *name,
	uint32_t dst_index,
	TextureID texture_handle  
); 

/// @brief Bind a single mip level + layer of an image to the binding
/// given by name.  The binding is assumed to not be an array type.
ev2::Result bind_image(
	GfxContext *ctx,
	BindingsID binding_handle,
	const char *name,
	ImageID image_handle,
	uint32_t level = 0,
	uint32_t layer = 0
);

ev2::Result bind_image_indexed(
	GfxContext *ctx,
	BindingsID binding_handle,
	const char *name,
	uint32_t dst_index,
	ImageID image_handle,
	uint32_t level = 0,
	uint32_t layer = 0
);

//------------------------------------------------------------------------------
// Rendering

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
	// Create a new depth buffer for this target
	RENDER_TARGET_CREATE_STENCIL_BIT = 0x8,
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

VkImageView get_render_target_color_view(RenderTargetID target);
ImageID get_render_target_color_image(RenderTargetID target);

void get_render_target_views(RenderTargetID handle, 
							  VkImageView* color, VkImageView* depth);
void get_render_target_images(RenderTargetID target,
							  ev2::ImageID *color, ev2::ImageID *depth);

ev2::Result begin_frame(GfxContext *ctx);
ev2::Result end_frame(GfxContext *ctx);

ViewID create_view(GfxContext *ctx, float view[], float proj[]);
void update_view(GfxContext *ctx, ViewID handle, float view[], float proj[]);
void destroy_view(GfxContext *ctx, ViewID handle);

struct Rect
{
	uint32_t x0, y0;
	uint32_t w, h;
};

constexpr Rect WHOLE_IMAGE = Rect{0, 0, UINT32_MAX, UINT32_MAX};

struct GfxPassInfo
{
	RenderTargetID target;
	ViewID view;
	Rect viewport = WHOLE_IMAGE;
	Rect scissor = WHOLE_IMAGE;

	bool clear_color : 1 = true;
	bool clear_depth : 1 = true;
};

PassID begin_gfx_pass(GfxContext *ctx, const GfxPassInfo *info);

// @brief Begin a render pass.  Configures bindings for pass-specific data  
// (view matrices, etc).    
PassID begin_gfx_pass(
	GfxContext *ctx, 
	RenderTargetID target, ViewID view,
	Rect viewport = WHOLE_IMAGE, Rect scissor = WHOLE_IMAGE
);

// @brief Begin a compute pass.  Does not bind any descriptor sets.   
PassID begin_compute_pass(
	GfxContext *ctx 
);

// @brief End a pass.  The ordering of this call determines the read/write
// dependencies between passes.
//
// E.g., if Pass A writes to X and is submitted first, and Pass B reads X, 
// and is submitted after, a dependency will be inserted between A and B
void end_pass(GfxContext *ctx, PassID pass);

//------------------------------------------------------------------------------
// command recording

void cmd_bind_resources(PassID pass_id, BindingsID bindings_id);
void cmd_bind_compute_pipeline(PassID pass_id, ComputePipelineID pipeline_id);
void cmd_bind_gfx_pipeline(PassID pass_id, GfxPipelineID pipeline_id);
void cmd_bind_index_buffer(PassID pass_id, BufferID buf, size_t offset);
void cmd_bind_vertex_buffer(PassID pass_id, BufferID buf, size_t offset);
void cmd_dispatch(PassID pass_id, uint32_t countx, uint32_t county, uint32_t countz);
void cmd_draw_indirect(PassID pass_id, BufferID buf, size_t offset, uint32_t count, uint32_t stride); 
void cmd_use_buffer(PassID pass_id, BufferID buf_id, Usage usage);
void cmd_use_image(PassID pass_id, ImageID img_id, Usage usage);
void cmd_push_constant(PassID pass_id, GfxPipelineID pipeline_id, 
					   uint32_t offset, uint32_t size, void *data);
void cmd_push_constant(PassID pass_id, ComputePipelineID pipeline_id, 
					   uint32_t offset, uint32_t size, void *data);
void cmd_custom(PassID pass_id, std::function<void(VkCommandBuffer)>&& callback);
};

#endif //EV2_PIPELINE_H
