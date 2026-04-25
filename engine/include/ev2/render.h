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
	Context *dev, 
	uint32_t w, 
	uint32_t h, 
	RenderTargetFlags flags
);
void destroy_render_target(
	Context *dev, 
	RenderTargetID id
);

RenderTargetAttachments get_render_target_attachments(
	Context *dev,
	RenderTargetID id
);

void begin_frame(Context *dev);
void end_frame(Context *dev);


MAKE_HANDLE(View);

ViewID create_view(Context *dev, float view[], float proj[]);
void update_view(Context *dev, ViewID handle, float view[], float proj[]);
void destroy_view(Context *dev, ViewID handle);

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
PassCtx begin_pass(Context *dev, RenderTargetID target, ViewID view,
				   Rect viewport, Rect scissor = {});
SyncID end_pass(Context *dev, PassCtx pass);

enum DrawMode
{
	MODE_TRIANGLES
};

void cmd_bind_gfx_pipeline(RecorderID rec, GraphicsPipelineID h);
void cmd_bind_index_buffer(RecorderID rec, BufferID buf);

void cmd_draw_screen_quad(RecorderID rec); 

};

#endif
