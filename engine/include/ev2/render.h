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

void resize_render_target(
	Device *dev, 
	RenderTargetID target, 
	uint32_t w, 
	uint32_t h
);

MAKE_HANDLE(View);

struct ViewInfo
{
	float view[4][4];
	float proj[4][4];
	float pos[3];
};

ViewID create_view(Device *dev, const ViewInfo *info);
void update_view(Device *dev, ViewID id, const ViewInfo *info);
void destroy_view(Device *dev, ViewID id);


MAKE_HANDLE(Frame);

FrameID begin_frame(Device *dev);
void end_frame(Device *dev, FrameID id);

MAKE_HANDLE(Pass);

struct PassCtx
{
	RecorderID rec;
	PassID id;
};

PassCtx begin_pass(Device *dev, FrameID frame);
SyncID end_pass(Device *dev, PassCtx pass);

enum DrawMode
{
	MODE_TRIANGLES
};

void cmd_draw(RecorderID rec, DrawMode mode, uint32_t vert_count); 

void present(Device *dev, RenderTargetID target, uint32_t w, uint32_t h);

};

//------------------------------------------------------------------------------

static inline void bigtest()
{
	ev2::Device *dev = ev2::create_device("./");

	ev2::GraphicsPipelineID screen_quad = ev2::load_graphics_pipeline(dev, "screen_quad");

	ev2::TextureAssetID saro_tex = ev2::load_texture_asset(dev, "images/saro.jpg");

	uint32_t w = 1024, h = 1024;

	ev2::ImageID swap_img[2] {
		ev2::create_image(dev, w, h, 1, ev2::IMAGE_FORMAT_RGBA8),
		ev2::create_image(dev, w, h, 1, ev2::IMAGE_FORMAT_RGBA8),
	};
	ev2::TextureID swap_tex[2] {
		ev2::create_texture(dev, swap_img[0],ev2::FILTER_BILINEAR),
		ev2::create_texture(dev, swap_img[1],ev2::FILTER_BILINEAR),
	};

	ev2::DescriptorLayoutID screen_quad_layout = 
		ev2::get_graphics_pipeline_layout(dev, screen_quad);

	ev2::DescriptorSetID screen_quad_set = ev2::create_descriptor_set(dev, screen_quad_layout);

	ev2::DescriptorSlot tex_slot = ev2::find_descriptor(screen_quad_layout, "u_tex");
	ev2::DescriptorSlot ubo_slot = ev2::find_descriptor(screen_quad_layout, "ubo");

	// Compute pipelines
	
	ev2::ComputePipelineID diffusion = ev2::load_compute_pipeline(dev, "diffusion");
	ev2::DescriptorLayoutID diffusion_layout = 
		ev2::get_compute_pipeline_layout(dev, diffusion);

	ev2::DescriptorSlot img_in_slot = ev2::find_descriptor(diffusion_layout, "img_in");
	ev2::DescriptorSlot img_out_slot = ev2::find_descriptor(diffusion_layout, "img_out");

	ev2::DescriptorSetID diffusion_set = ev2::create_descriptor_set(dev, diffusion_layout);

	int ctr = 0;

	uint32_t groups_x = w / 32;
	uint32_t groups_y = w / 32;

	while (1) {
		int curr = ctr;
		ctr = (ctr + 1) & 0x1;
		int next = ctr;

		ev2::FrameID frame = ev2::begin_frame(dev);

		ev2::bind_set_texture(dev, diffusion_set, img_in_slot, swap_tex[curr]);
		ev2::bind_set_texture(dev, diffusion_set, img_out_slot, swap_tex[next]);

		ev2::bind_set_texture(dev, screen_quad_set, tex_slot, swap_tex[curr]);

#define OPTION 2

#if OPTION != 3

#if OPTION == 2
		ev2::RecorderID rec = ev2::begin_commands(dev, ev2::MODE_SECONDARY);
#else 
		ev2::RecorderID rec = ev2::begin_commands(dev);
#endif

		ev2::cmd_bind_descriptor_set(rec, diffusion_set);
		ev2::cmd_dispatch(rec, diffusion, groups_x, groups_y, 1);
		ev2::cmd_use_texture(rec, swap_tex[curr], ev2::USAGE_SAMPLED_GRAPHICS);

		ev2::SyncID cmd_sync = ev2::end_commands(rec);
#endif

		// Option 1 : Finish these commands here (Bad in this case)
#if OPTION == 1
		ev2::finish(cmd_sync);
#endif

		ev2::PassCtx pass = ev2::begin_pass(dev, frame);

		// Option 2 : Execute compute as secondary commands
#if OPTION == 2
		ev2::cmd_execute(pass.rec, cmd_sync);
#endif

		// Option 3 : Record within pass
#if OPTION == 3
		ev2::cmd_bind_descriptor_set(pass.rec, diffusion_set);
		ev2::cmd_dispatch(pass.rec, diffusion, groups_x, groups_y, 1);
		ev2::cmd_barrier(pass.rec, swap_tex[curr], ev2::USAGE_SAMPLED_GRAPHICS, ev2::STAGE_FRAGMENT);
#endif

		ev2::cmd_bind_descriptor_set(pass.rec, screen_quad_set);

		// Don't need array bound since it uses vertex id
		ev2::cmd_draw(pass.rec, ev2::MODE_TRIANGLES, 6);

		ev2::SyncID pass_sync = ev2::end_pass(dev, pass);
		ev2::finish(pass_sync);

		ev2::end_frame(dev, frame);
	}
}

#endif
