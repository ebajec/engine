#ifndef EV2_RENDER_H
#define EV2_RENDER_H

#include "defines.h"
#include "context.h"
#include "resource.h"
#include "pipeline.h"

namespace ev2 {

struct DrawCommand 
{
    unsigned int  count;
    unsigned int  instanceCount;
    unsigned int  firstIndex;
    int  baseVertex;
    unsigned int  baseInstance;
};

MAKE_HANDLE(RenderTarget);

enum RenderTargetFlagBits
{
	RENDER_TARGET_COLOR_BIT = 0x1,
	RENDER_TARGET_DEPTH_BIT = 0x2,
};
typedef uint32_t RenderTargetFlags;

struct RenderTargetAttachments
{
	TextureID color;
	TextureID depth;
};

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

RenderTargetAttachments get_render_target_attachments(
	GfxContext *ctx,
	RenderTargetID id
);

ev2::Result begin_frame(GfxContext *ctx);
void end_frame(GfxContext *ctx);


MAKE_HANDLE(View);

ViewID create_view(GfxContext *ctx, float view[], float proj[]);
void update_view(GfxContext *ctx, ViewID handle, float view[], float proj[]);
void destroy_view(GfxContext *ctx, ViewID handle);

MAKE_HANDLE(Pass);

struct PassCtx
{
	RecorderID rec;
	PassID pass;
};

struct Rect
{
	uint32_t x0, y0;
	uint32_t w, h;
};

// @brief Begin a render pass.  Configures bindings for pass-specific data  
// (view matrices, etc).    
PassCtx begin_pass(GfxContext *ctx, RenderTargetID target, ViewID view,
				   Rect viewport, Rect scissor = {});
SyncID end_pass(GfxContext *ctx, PassCtx pass);

enum DrawMode
{
	MODE_TRIANGLES
};

void cmd_bind_gfx_pipeline(RecorderID rec, GfxPipelineID h);
void cmd_bind_index_buffer(RecorderID rec, BufferID buf);

void cmd_draw_screen_quad(RecorderID rec); 

};

#endif
