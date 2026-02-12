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

static std::vector<uint32_t> create_quad_indices(uint32_t n)
{
	std::vector<uint32_t> indices;
	for (uint32_t i = 0; i < n; ++i) {
		for (uint32_t j = 0; j < n; ++j) {
			uint32_t in = std::min(i + 1,n - 1);
			uint32_t jn = std::min(j + 1,n - 1);

			indices.push_back((n) * i  + j); 
			indices.push_back((n) * in + j); 
			indices.push_back((n) * in + jn); 

			indices.push_back((n) * i  + j);
			indices.push_back((n) * in + jn);
			indices.push_back((n) * i  + jn);
		}
	}

	return indices;
}

uint64_t upload_img_data(ev2::Device *dev, ev2::ImageID img, 
					 uint32_t w, uint32_t h)
{
	size_t size = w * h * sizeof(glm::vec4);

	ev2::UploadContext uc = ev2::begin_upload(dev, size, alignof(glm::vec4));

	glm::vec4 *pix = (glm::vec4*)uc.ptr;

	glm::vec2 center = glm::vec2(0.5f);

	const float sigma = 10.f;
	const float norm = 1.f/sqrtf(TWOPIf);
	const float power = 0*100.f;

	for (uint32_t i = 0; i < h; ++i) {
		for (uint32_t j = 0; j < w; ++j) {
			pix[i*w + j] = glm::vec4(0.f); 
		}
	}

	ev2::ImageUpload upload = {
		.src_offset = 0,
		.x = 0, 
		.y = 0,
		.w = w,
		.h = h,
	};

	return ev2::commit_image_uploads(dev, uc, img, &upload, 1);
}

struct WaveSim;

struct HeightmapViewerPanel
{
	App *app;
	std::unique_ptr<Panel> panel;

	MotionCamera control;
	glm::vec3 keydir = glm::vec3(0,0,0);

	struct RenderData {
		glm::mat4 proj = glm::mat4(1.f);
		glm::mat4 view = glm::mat4(1.f);

		uint32_t w = 0, h = 0;
		ev2::TextureID tex;

		ev2::ViewID camera;
		ev2::BufferID ibo; 
		ev2::GraphicsPipelineID pipeline;
		ev2::DescriptorSetID descriptors;
	} rd;

	int set_texture(ev2::Device *dev, ev2::TextureID tex);

	int init(App *app, ev2::Device *dev, ev2::TextureID tex);
	int update(ev2::Device *dev);
	void render(ev2::Device *dev);
	void destroy(ev2::Device *dev);
};

struct TextureViewerPanel
{
	App *app;

	std::unique_ptr<Panel> panel;

	struct RenderData {
		ev2::TextureID map;

		ev2::GraphicsPipelineID screen_quad;
		ev2::DescriptorSetID screen_quad_set;

		ev2::BindingSlot tex_slot;

		glm::vec2 center = glm::vec2(0);

		glm::mat4 proj = glm::mat4(1.f);
		glm::mat4 view = glm::mat4(1.f);
		ev2::ViewID camera;
	} rd;

	glm::vec2 world_cursor;

	int set_texture(ev2::Device *dev, ev2::TextureID tex);

	int init(App *app, ev2::Device *dev, ev2::TextureID tex);
	int update(ev2::Device *dev);
	void render(ev2::Device *dev);
	void destroy(ev2::Device *dev);

	glm::vec2 get_world_cursor_pos();
};

struct WaveSim
{
	ev2::ComputePipelineID sim_pipelines[2];

	uint32_t grid_w, grid_h;

	struct alignas(16) {
		alignas(8) glm::vec2 cursor1 = glm::vec2(0);
		alignas(8) glm::vec2 cursor2 = glm::vec2(0);

		float c = 12.f;  // wave speed
		float gradient = -0.0;
		float conj_gradient = 5;
		float laplacian = 0.9;
		float decay = 2.f;

		uint32_t active = 0;
	} uniforms {};

	ev2::BufferID ubo;

	ev2::ImageID swap_img[2] {};
	ev2::TextureID swap_tex[2] {};

	ev2::DescriptorSetID sim0_set;
	ev2::DescriptorSetID sim1_set;

	int swap_ctr = 0;

	ev2::BindingSlot img_in_slot;
	ev2::BindingSlot img_out_slot; 

	int init(ev2::Device *dev);
	int update(ev2::Device *dev);
	void destroy(ev2::Device *dev);
};

struct FluidApp : public App
{
	std::unique_ptr<WaveSim> sim;
	std::unique_ptr<TextureViewerPanel> texture_panel;
	std::unique_ptr<HeightmapViewerPanel> heightmap_panel;

	FluidApp() : App(1200, 500, "fluid") {
	}

	int initialize(int argc, char **argv);
	int update();
	void render();
	void destroy();
};

int HeightmapViewerPanel::init(App *app_, ev2::Device *dev, ev2::TextureID tex)
{
	app = app_;

	panel = std::make_unique<Panel>(dev, "3D view", 700, 0, 500, 500);

	//-----------------------------------------------------------------------------
	// Input

	app->insert_key_callback([this](int key, int scancode, int action, int mods){
		glfw_wasd_to_motion(this->keydir, key, action);
	});

	rd.camera = ev2::create_view(dev, nullptr, nullptr);

	control = MotionCamera::look_at(glm::vec3(0,0,0), glm::dvec3(1,1,1), glm::dvec3(0,0,1));

	//-----------------------------------------------------------------------------
	// Setup pipeline
	
	rd.pipeline = ev2::load_graphics_pipeline(dev, "pipelines/heightmap.yaml");

	if (!EV2_VALID(rd.pipeline))
		return EXIT_FAILURE;

	ev2::DescriptorLayoutID layout = ev2::get_graphics_pipeline_layout(dev, rd.pipeline);
	rd.descriptors = ev2::create_descriptor_set(dev, layout);

	int result = this->set_texture(dev, tex);

	if (result)
		return EXIT_FAILURE;

	return 0;
}

int HeightmapViewerPanel::set_texture(ev2::Device *dev, ev2::TextureID tex)
{
	uint32_t h, w;
	ev2::get_texture_dims(dev, tex, &w, &h, nullptr);

	//-----------------------------------------------------------------------------
	// Prepare index buffer
	if (rd.w != w || rd.h != h) {
		std::vector<uint32_t> indices = create_quad_indices(w);

		size_t indices_size = indices.size()*sizeof(uint32_t);

		rd.ibo = ev2::create_buffer(dev, indices_size);

		ev2::UploadContext uc = ev2::begin_upload(dev, indices_size, alignof(uint32_t)); 
		memcpy(uc.ptr, indices.data(), indices_size); 
		ev2::BufferUpload up = {.size = indices_size};
		uint64_t sync = ev2::commit_buffer_uploads(dev, uc, rd.ibo, &up, 1);
		ev2::wait_complete(dev, sync);
	}

	ev2::DescriptorLayoutID layout = ev2::get_graphics_pipeline_layout(dev, rd.pipeline);
	ev2::BindingSlot tex_slot = ev2::find_binding(layout, "u_tex");
	ev2::bind_texture(dev, rd.descriptors, tex_slot, tex);

	rd.w = w;
	rd.h = h;

	return 0;
}

int HeightmapViewerPanel::update(ev2::Device *dev)
{
	panel->imgui();

	glm::ivec2 panel_size = panel->get_size();

	float aspect = (float)panel_size.y/(float)panel_size.x;

	rd.proj = camera_proj_3d(PIf/4.f, aspect, 10.f, 0.01f);
	rd.view = control.get_view();
	
	ev2::update_view(dev, rd.camera, glm::value_ptr(rd.view), glm::value_ptr(rd.proj));

	static float speed = 1.f;

	if (app->input.mouse_mode == GLFW_CURSOR_DISABLED && panel->is_focused()) {
		glm::dvec2 delta = app->input.get_mouse_delta()/(double)panel_size.x;
		control.rotate(-delta.x, delta.y);
		control.move(app->input.dt * glm::dvec3(speed*keydir));

	}
	return 0;
}
void HeightmapViewerPanel::render(ev2::Device *dev)
{
	glm::ivec2 panel_size = panel->get_size();

	ev2::Rect rect = {
		.x0 = 0, .y0 = 0,
		.w = (uint32_t)panel_size.x, .h = (uint32_t)panel_size.y
	};
	ev2::PassCtx pass = ev2::begin_pass(dev, panel->get_target(), rd.camera, rect);
	ev2::cmd_bind_gfx_pipeline(pass.rec, rd.pipeline);
	ev2::cmd_bind_descriptor_set(pass.rec, rd.descriptors);
	ev2::cmd_bind_index_buffer(pass.rec, rd.ibo);

	uint32_t idx_count = 6 * rd.w * rd.h;

	glDrawElements(GL_TRIANGLES, idx_count, GL_UNSIGNED_INT, nullptr);

	ev2::SyncID sync = ev2::end_pass(dev, pass);
}

void HeightmapViewerPanel::destroy(ev2::Device *dev)
{
	ev2::destroy_descriptor_set(dev, rd.descriptors);
	ev2::destroy_buffer(dev, rd.ibo);
	ev2::destroy_view(dev, rd.camera);
	panel.reset();
}

//------------------------------------------------------------------------------
// 2D panel

glm::vec2 TextureViewerPanel::get_world_cursor_pos()
{
	glm::ivec2 panel_size = panel->get_size();
	glm::ivec2 panel_pos = panel->get_pos();
	glm::mat4 screen_to_world = glm::inverse(rd.proj*rd.view);

	glm::vec2 uv = (glm::vec2(app->input.mouse_pos[0]) -
		glm::vec2(panel_pos.x, panel_pos.y)) / 
		glm::vec2(panel_size.x, panel_size.y); 

	uv = glm::vec2(uv.x, 1.f - uv.y);

	uv = screen_to_world * glm::vec4(2.f*uv - glm::vec2(1.f),0,1);
	uv = 0.5f * (uv + glm::vec2(1.f));

	return glm::vec2(uv); 
}

int TextureViewerPanel::init(App *app_, ev2::Device *dev, ev2::TextureID tex) 
{
	app = app_;

	panel = std::make_unique<Panel>(dev,"Simulation",200,0,500,500);

	rd.screen_quad = ev2::load_graphics_pipeline(dev, "pipelines/screen_quad.yaml");

	if (!EV2_VALID(rd.screen_quad))
		return EXIT_FAILURE;

	rd.camera = ev2::create_view(dev, nullptr, nullptr);

	ev2::DescriptorLayoutID screen_quad_layout = 
		ev2::get_graphics_pipeline_layout(dev, rd.screen_quad);

	rd.screen_quad_set = ev2::create_descriptor_set(dev, screen_quad_layout);
	rd.tex_slot = ev2::find_binding(screen_quad_layout, "u_tex");

	int result = this->set_texture(dev, tex);

	if (result)
		return result;

	return 0;
}

int TextureViewerPanel::set_texture(ev2::Device *dev, ev2::TextureID tex)
{
	ev2::bind_texture(dev, rd.screen_quad_set, rd.tex_slot, tex);
	rd.map = tex;

	return 0;
}

int TextureViewerPanel::update(ev2::Device *dev)
{
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
			rd.center += 2.f*glm::vec2(glm::vec4(delta.x, -delta.y,0,0)/(aspect*zoom));
		}

		rd.view[3] = glm::vec4(glm::inverse(glm::mat2(rd.view))*rd.center,0,1);
	}
		
	ev2::update_view(dev, rd.camera, glm::value_ptr(rd.view), glm::value_ptr(rd.proj));
	return EXIT_SUCCESS;
}

void TextureViewerPanel::render(ev2::Device *dev)
{
	glm::ivec2 win_size = panel->get_size(); 

	ev2::RenderTargetID window_target = panel->get_target();
	ev2::Rect view_rect = {0,0, (uint32_t)win_size.x, (uint32_t)win_size.y};

	ev2::PassCtx pass = ev2::begin_pass(dev, window_target, rd.camera, view_rect);
	ev2::cmd_bind_gfx_pipeline(pass.rec, rd.screen_quad);
	ev2::cmd_bind_descriptor_set(pass.rec, rd.screen_quad_set);
	ev2::cmd_draw_screen_quad(pass.rec);
	ev2::SyncID pass_sync = ev2::end_pass(dev, pass);

	ev2::submit(pass_sync);
}

void TextureViewerPanel::destroy(ev2::Device *dev)
{
	ev2::destroy_descriptor_set(dev, rd.screen_quad_set);
	panel.reset();
}

//------------------------------------------------------------------------------
// Simulation

int WaveSim::init(ev2::Device *dev)
{
	sim_pipelines[0] = ev2::load_compute_pipeline(dev, 
		"shader/pde0.comp.spv");

	if (!EV2_VALID(sim_pipelines[0]))
		return EXIT_FAILURE;

	sim_pipelines[1] = ev2::load_compute_pipeline(dev, 
		"shader/pde1.comp.spv");

	if (!EV2_VALID(sim_pipelines[1]))
		return EXIT_FAILURE;

	grid_w = 256, grid_h = grid_w;

	swap_img[0] = ev2::create_image(dev, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_RGBA32F);
	swap_img[1] = ev2::create_image(dev, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_RGBA32F);

	swap_tex[0] = ev2::create_texture(dev, swap_img[0],ev2::FILTER_BILINEAR);
	swap_tex[1] = ev2::create_texture(dev, swap_img[1],ev2::FILTER_BILINEAR);

	ubo = ev2::create_buffer(dev, sizeof(uniforms));

	//------------------------------------------------------------------------------
	// Get shader resource locations and create descriptor sets

	ev2::DescriptorLayoutID layout = 
		ev2::get_compute_pipeline_layout(dev, sim_pipelines[0]);

	sim0_set = ev2::create_descriptor_set(dev, layout);

	sim1_set = ev2::create_descriptor_set(dev, layout);

	img_in_slot = ev2::find_binding(layout, "img_in");
	img_out_slot = ev2::find_binding(layout, "img_out");

	ev2::BindingSlot ubo_slot = ev2::find_binding(layout, "Uniforms");
	ev2::bind_buffer(dev, sim0_set, ubo_slot, ubo, 0, sizeof(uniforms));
	ev2::bind_buffer(dev, sim1_set, ubo_slot, ubo, 0, sizeof(uniforms));

	//------------------------------------------------------------------------------
	// Upload some stuff

	uint64_t sync = upload_img_data(dev,swap_img[0], grid_w, grid_h);
	ev2::flush_uploads(dev);

	ev2::wait_complete(dev, sync);

	return EXIT_SUCCESS;
}

int WaveSim::update(ev2::Device *dev)
{
	uint64_t sync = 0;
	ImGui::Begin("Editor");

	if (ImGui::CollapsingHeader("Simulation")) {
		ImGui::SliderFloat("gradient", &uniforms.gradient, -1.f, 1.f);
		ImGui::SliderFloat("conj_gradient", &uniforms.conj_gradient, 0.f, 10.f);
		ImGui::SliderFloat("laplacian", &uniforms.laplacian, 0.f, 1.f);
		ImGui::SliderFloat("decay", &uniforms.decay, 0.f, 20.f);

		ImGui::SliderFloat("wave_speed", &uniforms.c, 0.f, 12.f);

		if (ImGui::Button("reset")) {
			sync = upload_img_data(dev,swap_img[0], grid_w, grid_h);
		}
	}

	ImGui::End();
	ev2::UploadContext uc = ev2::begin_upload(dev,
		sizeof(uniforms), alignof(decltype(uniforms)));

	memcpy(uc.ptr, &uniforms, sizeof(uniforms));
	ev2::BufferUpload upload = {
		.src_offset = 0,
		.dst_offset = 0,
		.size = sizeof(uniforms),
	};

	sync = ev2::commit_buffer_uploads(dev, uc, ubo, &upload, 1);
	ev2::flush_uploads(dev);

	ev2::wait_complete(dev, sync);

	int img_A = 0;
	int img_B = 1;

	//-------------------------------------------------------------------
	// Simulation

	uint32_t grps_x = grid_w/32, grps_y = grid_h/32, grps_z = 1;

	ev2::bind_texture(dev, sim0_set, img_in_slot, swap_tex[img_A]);
	ev2::bind_texture(dev, sim0_set, img_out_slot, swap_tex[img_B]);

	ev2::bind_texture(dev, sim1_set, img_in_slot, swap_tex[img_B]);
	ev2::bind_texture(dev, sim1_set, img_out_slot, swap_tex[img_A]);

	ev2::RecorderID rec = ev2::begin_commands(dev);
	ev2::cmd_use_texture(rec, swap_tex[img_A], ev2::USAGE_STORAGE_READ_COMPUTE);
	ev2::cmd_use_texture(rec, swap_tex[img_B], ev2::USAGE_STORAGE_RW_COMPUTE);

	ev2::cmd_bind_descriptor_set(rec, sim0_set);
	ev2::cmd_bind_compute_pipeline(rec, sim_pipelines[0]);
	ev2::cmd_dispatch(rec, grps_x, grps_y, grps_z);

	ev2::cmd_use_texture(rec, swap_tex[img_B], ev2::USAGE_STORAGE_READ_COMPUTE);
	ev2::cmd_use_texture(rec, swap_tex[img_A], ev2::USAGE_STORAGE_RW_COMPUTE);
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

	ev2::cmd_bind_descriptor_set(rec, sim1_set);
	ev2::cmd_bind_compute_pipeline(rec, sim_pipelines[1]);
	ev2::cmd_dispatch(rec, grps_x, grps_y, grps_z);

	ev2::cmd_use_texture(rec, swap_tex[img_B], ev2::USAGE_SAMPLED_GRAPHICS);

	ev2::SyncID cmd_sync = ev2::end_commands(rec);

	return App::OK;
}

void WaveSim::destroy(ev2::Device *dev)
{
	ev2::destroy_descriptor_set(dev, sim0_set);
	ev2::destroy_descriptor_set(dev, sim1_set);

	ev2::destroy_texture(dev, swap_tex[0]);
	ev2::destroy_texture(dev, swap_tex[1]);

	ev2::destroy_image(dev, swap_img[0]);
	ev2::destroy_image(dev, swap_img[1]);
}

int FluidApp::initialize(int argc, char **argv)
{
	int result = App::initialize(argc, argv);
	if (result)
		return result;

	sim.reset(new WaveSim);
	texture_panel.reset(new TextureViewerPanel);
	heightmap_panel.reset(new HeightmapViewerPanel);

	result = sim->init(dev);
	if (result)
		return result;

	result = texture_panel->init(this, dev, sim->swap_tex[1]); 
	if (result)
		return result;

	result = heightmap_panel->init(this, dev, sim->swap_tex[1]); 
	if (result)
		return result;


	return result;
}
int FluidApp::update()
{
	int result = EXIT_SUCCESS;

	if ((result = App::update()))
		return result;

	if ((result = texture_panel->update(dev)))
		return result;

	if ((result = heightmap_panel->update(dev)))
		return result;

	if ((result = sim->update(dev)))
		return result;

	sim->uniforms.cursor1 = sim->uniforms.cursor2;
	sim->uniforms.cursor2 = texture_panel->get_world_cursor_pos();
	sim->uniforms.active = 
		this->input.right_mouse_pressed && 
		texture_panel->panel->is_content_selected();

	return result;
}
void FluidApp::render()
{
	texture_panel->render(dev);
	heightmap_panel->render(dev);
}
void FluidApp::destroy()
{
	heightmap_panel->destroy(dev);
	texture_panel->destroy(dev);
	sim->destroy(dev);

	App::terminate();
}

int main(int argc, char *argv[])
{
	std::unique_ptr<FluidApp> app (new FluidApp{});

	if (app->initialize(argc, argv) != App::OK)
		return EXIT_FAILURE;

	ev2::Device *dev = app->dev;

	while (
		app->update() == App::OK
	) {
		app->begin_frame();
		app->render();
		app->end_frame();
	}

	app->destroy();

	return EXIT_SUCCESS;
}
