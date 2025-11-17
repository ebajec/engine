#ifndef EV2_PIPELINE_H
#define EV2_PIPELINE_H

#include "ev2/device.h"
#include "ev2/resource.h"

namespace ev2 {

MAKE_HANDLE(GraphicsPipeline);
MAKE_HANDLE(ComputePipeline);
MAKE_HANDLE(DescriptorSet);
MAKE_HANDLE(DescriptorLayout);
MAKE_HANDLE(Shader);

struct DescriptorSlot
{
	uint16_t set;
	uint16_t id;
};

ShaderID load_shader(Device *dev, const char *path);

GraphicsPipelineID load_graphics_pipeline(Device *dev, const char *path);
ComputePipelineID load_compute_pipeline(Device *dev, const char *path);

DescriptorLayoutID get_graphics_pipeline_layout(
	Device *Dev, GraphicsPipelineID pipe);
DescriptorLayoutID get_compute_pipeline_layout(
	Device *Dev, ComputePipelineID pipe);

DescriptorSlot find_descriptor_slot(DescriptorLayoutID layout, const char *name);

DescriptorSetID create_descriptor_set(
	Device *dev, 
	DescriptorLayoutID layout,
	uint16_t index
);

void destroy_descriptor_set(
	Device *dev, 
	DescriptorLayoutID layout 
);

void descriptor_set_bind_buffer(
	Device *dev, 
	DescriptorSetID set, 
	DescriptorSlot slot, 
	BufferID buf, 
	size_t offset, 
	size_t size
); 

void descriptor_set_bind_texture(
	Device *dev, 
	DescriptorSetID set, 
	DescriptorSlot slot, 
	TextureID tex 
); 

void bind_descriptor_set(Device *dev, DescriptorSetID set_id);

};

static inline void bigtest()
{
	ev2::Device *dev = ev2::create_device("./");

	ev2::GraphicsPipelineID gfx_pipe = ev2::load_graphics_pipeline(dev, "screen_quad");

	ev2::ImageID image = ev2::load_image(dev, "images/saro.jpg");

	uint32_t w = 1024, h = 1024;

	ev2::ImageID swap_img[2] {
		ev2::create_image(dev, w, h, 1, ev2::IMAGE_FORMAT_RGBA8),
		ev2::create_image(dev, w, h, 1, ev2::IMAGE_FORMAT_RGBA8),
	};
	ev2::TextureID swap_tex[2] {
		ev2::create_texture(dev, swap_img[0],ev2::FILTER_BILINEAR),
		ev2::create_texture(dev, swap_img[1],ev2::FILTER_BILINEAR),
	};

	ev2::TextureID tex = ev2::create_texture(dev, image, ev2::FILTER_BILINEAR);

	ev2::DescriptorLayoutID gfx_layout = 
		ev2::get_graphics_pipeline_layout(dev, gfx_pipe);

	ev2::DescriptorSetID gfx_set = ev2::create_descriptor_set(dev, gfx_layout, 2);

	ev2::DescriptorSlot tex_slot = ev2::find_descriptor_slot(gfx_layout, "u_tex");
	ev2::DescriptorSlot ubo_slot = ev2::find_descriptor_slot(gfx_layout, "ubo");

	// Compute pipelines
	
	ev2::ComputePipelineID diffusion = ev2::load_compute_pipeline(dev, "diffusion");
	ev2::DescriptorLayoutID diffusion_layout = 
		ev2::get_compute_pipeline_layout(dev, diffusion);

	ev2::DescriptorSlot img_in_slot = ev2::find_descriptor_slot(diffusion_layout, "img_in");
	ev2::DescriptorSlot img_out_slot = ev2::find_descriptor_slot(diffusion_layout, "img_out");

	ev2::DescriptorSetID diffusion_set = ev2::create_descriptor_set(dev, diffusion_layout, 0);

	int ctr = 0;

	while (1) {
		int curr = ctr;
		ctr = (ctr + 1) & 0x1;
		int next = ctr;

		ev2::descriptor_set_bind_texture(dev, diffusion_set, img_in_slot, swap_tex[curr]);
		ev2::descriptor_set_bind_texture(dev, diffusion_set, img_in_slot, swap_tex[next]);

		ev2::bind_descriptor_set(dev, diffusion_set);

		ev2::descriptor_set_bind_texture(dev, gfx_set, tex_slot, swap_tex[curr]);
		ev2::bind_descriptor_set(dev, gfx_set);
	}
}

#endif //EV2_PIPELINE_H
