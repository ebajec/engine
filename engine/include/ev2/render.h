#ifndef EV2_RENDER_H
#define EV2_RENDER_H

#include "defines.h"
#include "device.h"
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

enum RenderTargetFlags
{
	RENDER_TARGET_COLOR_BIT = 0x1,
	RENDER_TARGET_DEPTH_BIT = 0x2,
};

RenderTargetID create_render_target(
	Device *dev, 
	uint32_t w, 
	uint32_t h, 
	RenderTargetFlags flags
);
void destroy_render_target(
	Device *dev, 
	RenderTargetID id
);

void begin_frame(Device *dev);
void end_frame(Device *dev);


MAKE_HANDLE(View);

ViewID create_view(Device *dev, float view[], float proj[]);
void update_view(Device *dev, ViewID handle, float view[], float proj[]);
void destroy_view(Device *dev, ViewID handle);

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
PassCtx begin_pass(Device *dev, RenderTargetID target, ViewID view,
				   Rect viewport, Rect scissor = {});
SyncID end_pass(Device *dev, PassCtx pass);

enum DrawMode
{
	MODE_TRIANGLES
};

void cmd_bind_pipeline(RecorderID rec, GraphicsPipelineID h);
void cmd_draw_screen_quad(RecorderID rec); 

};

#endif
