#include <engine/utils/log.h>

#include <ev2/device.h>
#include <ev2/render.h>
#include <ev2/resource.h>

#include <engine/utils/camera.h>
#include "app.h"

// glm
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

// std
#include <memory>
#include <cstdio>
#include <cstdlib>

void upload_img_data(ev2::Device *dev, ev2::ImageID img, 
					 uint32_t w, uint32_t h)
{
	size_t size = w * h * sizeof(uint32_t);

	ev2::UploadContext uc = ev2::begin_upload(dev, size, alignof(uint32_t));

	uint32_t *pix = (uint32_t*)uc.ptr;

	for (uint32_t i = 0; i < w*h; ++i) {
		pix[i] = (rand() % 0xFFFFFF) << 8 | 0xFF; 
	}

	ev2::ImageUpload upload = {
		.src_offset = 0,
		.x = 0, 
		.y = 0,
		.w = w,
		.h = h,
	};

	ev2::commit_image_uploads(dev, uc, img, &upload, 1);
}

struct MyStuff
{
	App *app;

	ev2::GraphicsPipelineID screen_quad;
	ev2::ComputePipelineID diffusion;

	ev2::ImageAssetID saro_img;
	ev2::TextureID saro_tex;

	uint32_t w, h;

	struct {
		float s;
	} uniforms;

	ev2::BufferID ubo;

	struct RenderData {
		glm::mat4 proj;
		glm::mat4 view;
		ev2::ViewID camera;
	} rd;

	glm::dvec2 center = glm::dvec2(0);

	ev2::ImageID swap_img[2] {};
	ev2::TextureID swap_tex[2] {};

	int swap_ctr = 0;

	ev2::DescriptorSetID saro_img_set;
	ev2::DescriptorSetID diffusion_set;

	ev2::BindingSlot tex_slot;

	ev2::BindingSlot img_in_slot;
	ev2::BindingSlot img_out_slot; 

	int init(ev2::Device *dev);
	int update(ev2::Device *dev);

	void render(ev2::Device *dev);
	void destroy(ev2::Device *dev);
};

int MyStuff::init(ev2::Device *dev)
{
	screen_quad = ev2::load_graphics_pipeline(dev, "pipelines/screen_quad.yaml");

	if (EV2_IS_NULL(screen_quad)) {
		return EXIT_FAILURE;
	}

	// Compute pipelines
	diffusion = ev2::load_compute_pipeline(dev, 
		"shader/diffusion.comp.spv");

	if (!diffusion.id) {
		ev2::destroy_device(dev);
		return EXIT_FAILURE;
	}

	saro_img = ev2::load_image_asset(dev, "image/saro.jpg");
	saro_tex = ev2::create_texture(
		dev,
		ev2::get_image_resource(dev, saro_img),
		ev2::FILTER_BILINEAR
	);

	w = 128, h = 64;

	swap_img[0] = ev2::create_image(dev, w, h, 1, ev2::IMAGE_FORMAT_RGBA8);
	swap_img[1] = ev2::create_image(dev, w, h, 1, ev2::IMAGE_FORMAT_RGBA8);

	swap_tex[0] = ev2::create_texture(dev, swap_img[0],ev2::FILTER_BILINEAR);
	swap_tex[1] = ev2::create_texture(dev, swap_img[1],ev2::FILTER_BILINEAR);

	ubo = ev2::create_buffer(dev, sizeof(uniforms));

	rd.camera = ev2::create_view(dev, nullptr, nullptr);

	//------------------------------------------------------------------------------
	// Get shader resource locations and create descriptor sets

	ev2::DescriptorLayoutID screen_quad_layout = 
		ev2::get_graphics_pipeline_layout(dev, screen_quad);

	ev2::DescriptorLayoutID diffusion_layout = 
		ev2::get_compute_pipeline_layout(dev, diffusion);

	saro_img_set = ev2::create_descriptor_set(dev, screen_quad_layout);
	diffusion_set = ev2::create_descriptor_set(dev, diffusion_layout);

	tex_slot = ev2::find_binding(screen_quad_layout, "u_tex");

	img_in_slot = ev2::find_binding(diffusion_layout, "img_in");
	img_out_slot = ev2::find_binding(diffusion_layout, "img_out");

	ev2::BindingSlot ubo_slot = ev2::find_binding(screen_quad_layout, "Uniforms");
	ev2::bind_buffer(dev, saro_img_set, ubo_slot, ubo, 0, sizeof(uniforms));

	//------------------------------------------------------------------------------
	// Upload some stuff

	upload_img_data(dev,swap_img[0], w, h);
	ev2::flush_uploads(dev);

	return EXIT_SUCCESS;
}

int MyStuff::update(ev2::Device *dev)
{
	ImGui::Begin("Editor");
	ImGui::SliderFloat("s", &uniforms.s, 0.f, 1.f);
	ImGui::End();

	ev2::UploadContext uc = ev2::begin_upload(dev, sizeof(uniforms), 4);
	memcpy(uc.ptr, &uniforms, sizeof(uniforms));

	ev2::BufferUpload upload = {
		.src_offset = 0,
		.dst_offset = 0,
		.size = sizeof(uniforms),
	};

	ev2::commit_buffer_uploads(dev, uc, ubo, &upload, 1);
	ev2::flush_uploads(dev);

	rd.proj = camera_proj_2d((float)app->win.height/(float)app->win.width, 1.f);
	rd.view = glm::mat4(1.f);

	if (app->input.left_mouse_pressed && !app->input.mouse_in_gui) {
		glm::dvec2 delta = app->input.get_mouse_delta()/(double)app->win.width; 
		center += 2.*glm::dvec2(delta.x, -delta.y);
	}

	rd.view[3] = glm::vec4(center,0,1);

	ev2::update_view(dev, rd.camera, glm::value_ptr(rd.view), glm::value_ptr(rd.proj));

	return App::OK;
}

void MyStuff::render(ev2::Device *dev)
{
	ev2::Rect view_rect = { .x0 = 0, .y0 = 0,
		.w = (uint32_t)app->win.width,
		.h = (uint32_t)app->win.height
	};

	int curr = swap_ctr;
	swap_ctr ^= 0x1;
	int next = swap_ctr;

	ev2::bind_texture(dev, diffusion_set, img_in_slot, swap_tex[curr]);
	ev2::bind_texture(dev, diffusion_set, img_out_slot, swap_tex[next]);

	//ev2::bind_texture(dev, screen_quad_set, tex_slot, swap_tex[curr]);
	ev2::bind_texture(dev, saro_img_set, tex_slot, saro_tex);

	ev2::RecorderID rec = ev2::begin_commands(dev);
	ev2::cmd_bind_descriptor_set(rec, diffusion_set);

	ev2::cmd_dispatch(rec, diffusion, w/32, h/32, 1);
	ev2::cmd_use_texture(rec, swap_tex[curr], ev2::USAGE_SAMPLED_GRAPHICS);
	ev2::SyncID cmd_sync = ev2::end_commands(rec);

	ev2::PassCtx pass = ev2::begin_pass(dev, {}, rd.camera, view_rect);
	ev2::cmd_bind_pipeline(pass.rec, screen_quad);
	ev2::cmd_bind_descriptor_set(pass.rec, saro_img_set);
	ev2::cmd_draw_screen_quad(pass.rec);
	ev2::SyncID pass_sync = ev2::end_pass(dev, pass);

	ev2::submit(pass_sync);
}

void MyStuff::destroy(ev2::Device *dev)
{
	ev2::destroy_descriptor_set(dev, diffusion_set);
	ev2::destroy_descriptor_set(dev, saro_img_set);

	ev2::destroy_texture(dev, swap_tex[0]);
	ev2::destroy_texture(dev, swap_tex[1]);

	ev2::destroy_image(dev, swap_img[0]);
	ev2::destroy_image(dev, swap_img[1]);
}

int main(int argc, char *argv[])
{
	std::unique_ptr<App> app (new App{
		.win = {
			.width = 1000,
			.height = 1000,
			.title = "ev2"
		}
	});

	if (app->initialize(argc, argv) != App::OK)
		return EXIT_FAILURE;

	ev2::Device *dev = app->dev;

	MyStuff data = {
		.app = app.get()
	};

	if (data.init(dev) != EXIT_SUCCESS)
		return EXIT_FAILURE;

	while (
		app->update() == App::OK &&
		data.update(dev) == App::OK
	) {
		app->begin_frame();
		data.render(dev);
		app->end_frame();
	}

	data.destroy(dev);
	app->terminate();

	return EXIT_SUCCESS;
}
