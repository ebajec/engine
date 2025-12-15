#include "ev2/render.h"
#include "device_impl.h"

namespace ev2 {

RenderTargetID create_render_target(
	Device *dev, 
	uint32_t w, 
	uint32_t h, 
	RenderTargetFlags flags
)
{
	return EV2_NULL_HANDLE(RenderTarget);
}

void destroy_render_target(
	Device *dev, 
	RenderTargetID id
)
{
}

void resize_render_target(
	Device *dev, 
	RenderTargetID target, 
	uint32_t w, 
	uint32_t h
)
{
}

ViewID create_view(Device *dev, const ViewInfo *info)
{
	return EV2_NULL_HANDLE(View);
}

void update_view(Device *dev, ViewID id, const ViewInfo *info) {
}

void destroy_view(Device *dev, ViewID id)
{
}

FrameID begin_frame(Device *dev)
{
	return EV2_NULL_HANDLE(Frame);
}

void end_frame(Device *dev, FrameID id)
{
	if (dev->assets->reloader) {
		dev->assets->reloader->update();
	}
}

PassCtx begin_pass(Device *dev, FrameID frame)
{
	PassCtx ctx {};

	return ctx;
}

SyncID end_pass(Device *dev, PassCtx pass)
{
	return EV2_NULL_HANDLE(Sync);
}

void cmd_draw(RecorderID rec, DrawMode mode, uint32_t vert_count)
{

}

void present(Device *dev, RenderTargetID target, uint32_t w, uint32_t h)
{

}

};

