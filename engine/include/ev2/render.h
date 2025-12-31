#ifndef EV2_RENDER_H
#define EV2_RENDER_H

#include "defines.h"
#include "device.h"
#include "resource.h"
#include "pipeline.h"

namespace ev2 {

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

MAKE_HANDLE(View);

ViewID create_view(Device *dev, float view[], float proj[]);
void update_view(Device *dev, ViewID handle, float view[], float proj[]);
void destroy_view(Device *dev, ViewID handle);

MAKE_HANDLE(Frame);

void begin_frame(Device *dev);
void end_frame(Device *dev);

MAKE_HANDLE(Pass);

struct PassCtx
{
	RecorderID rec;
	PassID id;
};

PassCtx begin_pass(Device *dev, RenderTargetID target, ViewID view);
SyncID end_pass(Device *dev, PassCtx pass);

enum DrawMode
{
	MODE_TRIANGLES
};

void cmd_draw(RecorderID rec, DrawMode mode, uint32_t vert_count); 

};

#endif
