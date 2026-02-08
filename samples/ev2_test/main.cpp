#include <engine/utils/log.h>

#include <ev2/device.h>
#include <ev2/render.h>
#include <ev2/resource.h>

#include <engine/utils/camera.h>
#include <engine/utils/geometry.h>

#include "app.h"
#include "panel.h"

// glm
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

// std
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cmath>

static inline uint8_t float01_to_u8(float x)
{
    if (!std::isfinite(x)) x = 0.0f;

    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;

    long v = lrintf(x * 255.0f);

    if (v < 0)   v = 0;
    if (v > 255) v = 255;

    return (uint8_t)v;
}

uint32_t rgba32f2abgr8(float r, float g, float b, float a)
{
    uint32_t R = (uint32_t)float01_to_u8(r);
    uint32_t G = (uint32_t)float01_to_u8(g);
    uint32_t B = (uint32_t)float01_to_u8(b);
    uint32_t A = (uint32_t)float01_to_u8(a);

    return (A << 24) | (B << 16) | (G << 8) | (R << 0);
}
void upload_img_data(ev2::Device *dev, ev2::ImageID img, 
					 uint32_t w, uint32_t h)
{
	size_t size = w * h * sizeof(uint32_t);

	ev2::UploadContext uc = ev2::begin_upload(dev, size, alignof(uint32_t));

	uint32_t *pix = (uint32_t*)uc.ptr;

	glm::vec2 center = glm::vec2(0.5f);

	const float sigma = 10.f;
	const float norm = 1.f/sqrtf(TWOPIf);
	const float power = 100.f;

	for (uint32_t i = 0; i < h; ++i) {
		for (uint32_t j = 0; j < w; ++j) {
			glm::vec2 p = glm::vec2(i,j)/glm::vec2(w,h) - center;

			float f = power*norm*sigma*expf(-sigma*sigma*glm::dot(p,p));
	  	
			pix[i*w + j] = rgba32f2abgr8(f*p.x*p.x,f*p.y*p.y,0.f,1.f); 
		}
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

struct Simulation
{
	App *app;

	std::unique_ptr<Panel> panel;

	ev2::GraphicsPipelineID screen_quad;
	ev2::ComputePipelineID diffusion;

	ev2::ImageAssetID saro_img;
	ev2::TextureID saro_tex;

	uint32_t sim_w, sim_h;

	struct {
		glm::vec2 cursor;
		uint32_t active;
		float s;
	} uniforms;

	ev2::BufferID ubo;

	struct RenderData {
		glm::mat4 proj = glm::mat4(1.f);
		glm::mat4 view = glm::mat4(1.f);
		ev2::ViewID camera;
	} rd;

	glm::vec2 center = glm::vec2(0);

	ev2::ImageID swap_img[2] {};
	ev2::TextureID swap_tex[2] {};

	int swap_ctr = 0;

	ev2::DescriptorSetID screen_quad_set;
	ev2::DescriptorSetID diffusion_set;

	ev2::BindingSlot tex_slot;

	ev2::BindingSlot img_in_slot;
	ev2::BindingSlot img_out_slot; 

	int init(ev2::Device *dev);
	int update(ev2::Device *dev);

	void render(ev2::Device *dev);
	void destroy(ev2::Device *dev);
};

int Simulation::init(ev2::Device *dev)
{
	panel = std::make_unique<Panel>(dev,"Simulation",250,0,500,500);

	screen_quad = ev2::load_graphics_pipeline(dev, "pipelines/screen_quad.yaml");

	if (EV2_IS_NULL(screen_quad)) {
		return EXIT_FAILURE;
	}

	// Compute pipelines
	diffusion = ev2::load_compute_pipeline(dev, 
		"shader/diffusion.comp.spv");

	if (!EV2_VALID(diffusion)) {
		ev2::destroy_device(dev);
		return EXIT_FAILURE;
	}

	saro_img = ev2::load_image_asset(dev, "image/saro.jpg");
	saro_tex = ev2::create_texture(
		dev,
		ev2::get_image_resource(dev, saro_img),
		ev2::FILTER_BILINEAR
	);

	sim_w = 2048, sim_h = sim_w;

	swap_img[0] = ev2::create_image(dev, sim_w, sim_h, 1, ev2::IMAGE_FORMAT_RGBA8);
	swap_img[1] = ev2::create_image(dev, sim_w, sim_h, 1, ev2::IMAGE_FORMAT_RGBA8);

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

	screen_quad_set = ev2::create_descriptor_set(dev, screen_quad_layout);
	diffusion_set = ev2::create_descriptor_set(dev, diffusion_layout);

	tex_slot = ev2::find_binding(screen_quad_layout, "u_tex");

	img_in_slot = ev2::find_binding(diffusion_layout, "img_in");
	img_out_slot = ev2::find_binding(diffusion_layout, "img_out");

	ev2::BindingSlot ubo_slot = ev2::find_binding(screen_quad_layout, "Uniforms");
	ev2::bind_buffer(dev, screen_quad_set, ubo_slot, ubo, 0, sizeof(uniforms));

	//------------------------------------------------------------------------------
	// Upload some stuff

	upload_img_data(dev,swap_img[0], sim_w, sim_h);
	ev2::flush_uploads(dev);

	return EXIT_SUCCESS;
}

int Simulation::update(ev2::Device *dev)
{
	ImGui::Begin("Editor");

	if (ImGui::CollapsingHeader("Simulation")) {
		ImGui::SliderFloat("gradient", &uniforms.s, 0.f, 1.f);
		if (ImGui::Button("reset")) {
			upload_img_data(dev,swap_img[swap_ctr], sim_w, sim_h);
		}
	}
	ImGui::End();
	panel->imgui(); 

	glm::ivec2 panel_size = panel->get_size();
	glm::ivec2 panel_pos = panel->get_pos();

	float aspect = (float)panel_size.y/(float)panel_size.x;
	float zoom = pow(2, app->input.scroll.y);

	rd.proj = camera_proj_2d(aspect, zoom);

	glm::mat4 p_inv = glm::inverse(rd.proj);

	if (panel->is_content_selected()) {
		if (app->input.left_mouse_pressed && panel->is_content_selected()) {
			glm::dvec2 delta = app->input.get_mouse_delta()/(double)panel->get_size().x; 
			center += 2.f*glm::vec2(glm::vec4(delta.x, -delta.y,0,0)/(aspect*zoom));
		}

		rd.view[3] = glm::vec4(glm::inverse(glm::mat2(rd.view))*center,0,1);
	}

	glm::mat4 screen_to_world = glm::inverse(rd.view)*p_inv;

	glm::vec2 uv = (glm::vec2(app->input.mouse_pos[0]) -
		glm::vec2(panel_pos.x, panel_pos.y)) / 
		glm::vec2(panel_size.x, panel_size.y); 

	uv = glm::vec2(uv.x, 1.f - uv.y);
	uv = screen_to_world * glm::vec4(2.f*uv - glm::vec2(1.f),0,1);
	uv = 0.5f * (uv + glm::vec2(1.f));

	uniforms.cursor = glm::vec2(uv); 
		
	uniforms.active = 
		app->input.right_mouse_pressed && 
		panel->is_content_selected();

	ev2::UploadContext uc = ev2::begin_upload(dev, sizeof(uniforms), 4);
	memcpy(uc.ptr, &uniforms, sizeof(uniforms));

	ev2::BufferUpload upload = {
		.src_offset = 0,
		.dst_offset = 0,
		.size = sizeof(uniforms),
	};

	ev2::commit_buffer_uploads(dev, uc, ubo, &upload, 1);
	ev2::flush_uploads(dev);

ev2::update_view(dev, rd.camera, glm::value_ptr(rd.view), glm::value_ptr(rd.proj));

	return App::OK;
}

void Simulation::render(ev2::Device *dev)
{
	int curr = swap_ctr;
	swap_ctr ^= 0x1;
	int next = swap_ctr;

	ev2::bind_texture(dev, diffusion_set, img_in_slot, swap_tex[curr]);
	ev2::bind_texture(dev, diffusion_set, img_out_slot, swap_tex[next]);

	ev2::RecorderID rec = ev2::begin_commands(dev);
	ev2::cmd_bind_compute_pipeline(rec, diffusion);
	ev2::cmd_bind_descriptor_set(rec, diffusion_set);
	ev2::cmd_use_texture(rec, swap_tex[curr], ev2::USAGE_STORAGE_READ_COMPUTE);
	ev2::cmd_dispatch(rec, sim_w/32, sim_h/32, 1);
	ev2::cmd_use_texture(rec, swap_tex[curr], ev2::USAGE_SAMPLED_GRAPHICS);
	ev2::SyncID cmd_sync = ev2::end_commands(rec);

	ev2::bind_texture(dev, screen_quad_set, tex_slot, swap_tex[curr]);

	glm::ivec2 win_size = panel->get_size(); 

	ev2::RenderTargetID window_target = panel->get_target();
	ev2::Rect view_rect = {0,0, (uint32_t)win_size.x, (uint32_t)win_size.y};

	ev2::PassCtx pass = ev2::begin_pass(dev, window_target, rd.camera, view_rect);
	ev2::cmd_bind_gfx_pipeline(pass.rec, screen_quad);
	ev2::cmd_bind_descriptor_set(pass.rec, screen_quad_set);
	ev2::cmd_draw_screen_quad(pass.rec);
	ev2::SyncID pass_sync = ev2::end_pass(dev, pass);

	ev2::submit(pass_sync);
}

void Simulation::destroy(ev2::Device *dev)
{
	ev2::destroy_descriptor_set(dev, diffusion_set);
	ev2::destroy_descriptor_set(dev, screen_quad_set);

	ev2::destroy_texture(dev, swap_tex[0]);
	ev2::destroy_texture(dev, swap_tex[1]);

	ev2::destroy_image(dev, swap_img[0]);
	ev2::destroy_image(dev, swap_img[1]);

	panel.reset();
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

	Simulation sim = {
		.app = app.get()
	};

	if (sim.init(dev) != EXIT_SUCCESS)
		return EXIT_FAILURE;

	while (
		app->update() == App::OK &&
		sim.update(dev) == App::OK
	) {
		app->begin_frame();
		sim.render(dev);
		app->end_frame();
	}

	sim.destroy(dev);
	app->terminate();

	return EXIT_SUCCESS;
}
