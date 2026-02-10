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


glm::vec2 swirl(glm::vec2 p, glm::vec2 c)
{
	glm::vec2 r = p - glm::vec2(c);

	r *= 20;

	return (0.1f * r + glm::vec2(r.y,-r.x))/(powf(1.f + glm::dot(r,r),1.5));
}

float urandf()
{
	return (float)rand()/(float)RAND_MAX;
}

glm::vec2 v_cool(glm::vec2 p) {
	static uint init = 0;
	static constexpr uint32_t count = 15;
	static glm::vec2 pos[count];

	if (!init++) {
		for (uint32_t i = 0; i < count; ++i) {
			pos[i] = 1.f - 2.f*glm::vec2(urandf(), urandf());
		}
	}

	glm::vec2 out = glm::vec2(0);

	for (uint32_t i = 0; i < count; ++i) {
		out += ((i & 0x1) ? -1.f : 1.f) * swirl(p, pos[i]);	
	}

	return out;
}

void upload_img_data(ev2::Device *dev, ev2::ImageID img, 
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
			glm::vec2 p = glm::vec2(1.f) - 2.f*glm::vec2(j,i)/glm::vec2(w,h);

			float f = power*norm*sigma*expf(-sigma*sigma*glm::dot(p,p));

			glm::vec2 v = 0.f*v_cool(p);
	  	
			pix[i*w + j] = glm::vec4(f*p.x*p.x,f*p.y*p.y,v); 
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

struct Simulation;

struct Simulation3DPanel
{
	App *app;
	Simulation *sim;
	std::unique_ptr<Panel> panel;

	MotionCamera control;
	glm::vec3 keydir;

	struct RenderData {
		glm::mat4 proj = glm::mat4(1.f);
		glm::mat4 view = glm::mat4(1.f);

		ev2::ViewID camera;
		ev2::BufferID ibo; 
		ev2::GraphicsPipelineID pipeline;
		ev2::DescriptorSetID descriptors;
	} rd;

	int init(Simulation *sim, ev2::Device *dev);
	int update(ev2::Device *dev);
	void render(ev2::Device *dev);
	void destroy(ev2::Device *dev);
};

struct Simulation2DPanel
{
	App *app;
	Simulation *sim;

	std::unique_ptr<Panel> panel;

	struct RenderData {
		ev2::TextureID map;

		ev2::GraphicsPipelineID screen_quad;
		ev2::DescriptorSetID screen_quad_set;
		ev2::ImageAssetID saro_img;
		ev2::TextureID saro_tex;

		ev2::BindingSlot tex_slot;

		glm::vec2 center = glm::vec2(0);

		glm::mat4 proj = glm::mat4(1.f);
		glm::mat4 view = glm::mat4(1.f);
		ev2::ViewID camera;
	} rd;

	glm::vec2 world_cursor;

	int init(Simulation *sim, ev2::Device *dev);
	int update(ev2::Device *dev);
	void render(ev2::Device *dev);
	void destroy(ev2::Device *dev);

	glm::vec2 get_world_cursor_pos();
};

struct Simulation
{
	App *app;

	Simulation2DPanel panel_2d;
	Simulation3DPanel panel_3d;

	ev2::ComputePipelineID sim_pipelines[2];

	uint32_t grid_w, grid_h;

	struct {
		glm::vec2 cursor1 = glm::vec2(0);
		glm::vec2 cursor2 = glm::vec2(0);
		uint32_t active;
		float s;

		// wave speed
		float c = 12.f;
		float gradient = -0.0;
		float conj_gradient = 5;
		float laplacian = 0.9;
		float decay = 0.8;
	} uniforms;

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

	void render(ev2::Device *dev);
	void destroy(ev2::Device *dev);
};

int Simulation3DPanel::init(Simulation *sim_, ev2::Device *dev)
{
	sim = sim_;
	app = sim->app;

	panel = std::make_unique<Panel>(dev, "3D view", 700, 0, 500, 500);

	//-----------------------------------------------------------------------------
	// Input

	app->insert_key_callback([this](int key, int scancode, int action, int mods){
		glfw_wasd_to_motion(this->keydir, key, action);
	});

	rd.camera = ev2::create_view(dev, nullptr, nullptr);

	control = MotionCamera::look_at(glm::vec3(0,0,0), glm::dvec3(1,1,1), glm::dvec3(0,0,1));

	//-----------------------------------------------------------------------------
	// Prepare index buffer
	std::vector<uint32_t> indices = create_quad_indices(sim->grid_w);

	size_t indices_size = indices.size()*sizeof(uint32_t);

	rd.ibo = ev2::create_buffer(dev, indices_size);

	ev2::UploadContext uc = ev2::begin_upload(dev, indices_size, alignof(uint32_t)); 
	memcpy(uc.ptr, indices.data(), indices_size); 
	ev2::BufferUpload up = {
		.src_offset = 0,
		.dst_offset = 0, 
		.size = indices_size
	};
	uint64_t sync = ev2::commit_buffer_uploads(dev, uc, rd.ibo, &up, 1);
	ev2::wait_complete(dev, sync);

	//-----------------------------------------------------------------------------
	// Setup pipeline
	
	rd.pipeline = ev2::load_graphics_pipeline(dev, "pipelines/heightmap.yaml");

	if (!EV2_VALID(rd.pipeline))
		return EXIT_FAILURE;

	ev2::DescriptorLayoutID layout = ev2::get_graphics_pipeline_layout(dev, rd.pipeline);
	rd.descriptors = ev2::create_descriptor_set(dev, layout);

	ev2::BindingSlot tex_slot = ev2::find_binding(layout, "u_tex");
	ev2::bind_texture(dev, rd.descriptors, tex_slot, sim->swap_tex[1]);

	if (!EV2_VALID(rd.pipeline))
		return EXIT_FAILURE;

	return 0;
}
int Simulation3DPanel::update(ev2::Device *dev)
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
void Simulation3DPanel::render(ev2::Device *dev)
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

	uint32_t idx_count = 6 * sim->grid_w * sim->grid_h;

	glDrawElements(GL_TRIANGLES, idx_count, GL_UNSIGNED_INT, nullptr);

	ev2::SyncID sync = ev2::end_pass(dev, pass);
}

void Simulation3DPanel::destroy(ev2::Device *dev)
{
	ev2::destroy_descriptor_set(dev, rd.descriptors);
	ev2::destroy_buffer(dev, rd.ibo);
	ev2::destroy_view(dev, rd.camera);
	panel.reset();
}

//------------------------------------------------------------------------------
// 2D panel

glm::vec2 Simulation2DPanel::get_world_cursor_pos()
{
	glm::ivec2 panel_size = panel->get_size();
	glm::ivec2 panel_pos = panel->get_pos();
	glm::mat4 screen_to_world = glm::inverse(rd.proj*rd.view);

	glm::vec2 uv = (glm::vec2(sim->app->input.mouse_pos[0]) -
		glm::vec2(panel_pos.x, panel_pos.y)) / 
		glm::vec2(panel_size.x, panel_size.y); 

	uv = glm::vec2(uv.x, 1.f - uv.y);

	uv = screen_to_world * glm::vec4(2.f*uv - glm::vec2(1.f),0,1);
	uv = 0.5f * (uv + glm::vec2(1.f));

	return glm::vec2(uv); 
}

int Simulation2DPanel::init(Simulation *sim_, ev2::Device *dev) 
{
	sim = sim_;
	rd.map = sim_->swap_tex[1];

	panel = std::make_unique<Panel>(dev,"Simulation",200,0,500,500);

	rd.screen_quad = ev2::load_graphics_pipeline(dev, "pipelines/screen_quad.yaml");

	if (!EV2_VALID(rd.screen_quad))
		return EXIT_FAILURE;

	rd.saro_img = ev2::load_image_asset(dev, "image/saro.jpg");
	rd.saro_tex = ev2::create_texture(
		dev,
		ev2::get_image_resource(dev, rd.saro_img),
		ev2::FILTER_BILINEAR
	);

	rd.camera = ev2::create_view(dev, nullptr, nullptr);

	ev2::DescriptorLayoutID screen_quad_layout = 
		ev2::get_graphics_pipeline_layout(dev, rd.screen_quad);

	rd.screen_quad_set = ev2::create_descriptor_set(dev, screen_quad_layout);
	rd.tex_slot = ev2::find_binding(screen_quad_layout, "u_tex");

	ev2::BindingSlot ubo_slot = ev2::find_binding(screen_quad_layout, "Uniforms");

	ev2::bind_buffer(dev, rd.screen_quad_set, ubo_slot, sim->ubo, 0, sizeof(sim->uniforms));

	return 0;
}

int Simulation2DPanel::update(ev2::Device *dev)
{
	panel->imgui(); 

	glm::ivec2 panel_size = panel->get_size();
	glm::ivec2 panel_pos = panel->get_pos();

	float aspect = (float)panel_size.y/(float)panel_size.x;
	float zoom = pow(2, sim->app->input.scroll.y);

	rd.proj = camera_proj_2d(aspect, zoom);

	glm::mat4 p_inv = glm::inverse(rd.proj);

	if (panel->is_content_selected()) {
		if (sim->app->input.left_mouse_pressed && panel->is_content_selected()) {
			glm::dvec2 delta = sim->app->input.get_mouse_delta()/(double)panel->get_size().x; 
			rd.center += 2.f*glm::vec2(glm::vec4(delta.x, -delta.y,0,0)/(aspect*zoom));
		}

		rd.view[3] = glm::vec4(glm::inverse(glm::mat2(rd.view))*rd.center,0,1);
	}
		
	ev2::update_view(dev, rd.camera, glm::value_ptr(rd.view), glm::value_ptr(rd.proj));

	return EXIT_SUCCESS;
}

void Simulation2DPanel::render(ev2::Device *dev)
{
	ev2::bind_texture(dev, rd.screen_quad_set, rd.tex_slot, rd.map);

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

void Simulation2DPanel::destroy(ev2::Device *dev)
{
	ev2::destroy_descriptor_set(dev, rd.screen_quad_set);
	ev2::destroy_texture(dev, rd.saro_tex);
	panel.reset();
}

//------------------------------------------------------------------------------
// Simulation

int Simulation::init(ev2::Device *dev)
{
	sim_pipelines[0] = ev2::load_compute_pipeline(dev, 
		"shader/pde0.comp.spv");

	if (!EV2_VALID(sim_pipelines[0]))
		return EXIT_FAILURE;

	sim_pipelines[1] = ev2::load_compute_pipeline(dev, 
		"shader/pde1.comp.spv");

	if (!EV2_VALID(sim_pipelines[1]))
		return EXIT_FAILURE;

	grid_w = 2048, grid_h = grid_w;

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

	//------------------------------------------------------------------------------
	// Upload some stuff

	upload_img_data(dev,swap_img[0], grid_w, grid_h);
	ev2::flush_uploads(dev);

	int result = panel_2d.init(this, dev); 
	if (result)
		return result;

	result = panel_3d.init(this, dev); 
	if (result)
		return result;

	return EXIT_SUCCESS;
}

int Simulation::update(ev2::Device *dev)
{
	ImGui::Begin("Editor");

	if (ImGui::CollapsingHeader("Simulation")) {

		ImGui::SliderFloat("2D shading", &uniforms.s, 0.f, 1.f);

		ImGui::SliderFloat("gradient", &uniforms.gradient, -1.f, 1.f);
		ImGui::SliderFloat("conj_gradient", &uniforms.conj_gradient, 0.f, 10.f);
		ImGui::SliderFloat("laplacian", &uniforms.laplacian, 0.f, 1.f);
		ImGui::SliderFloat("decay", &uniforms.decay, 0.f, 20.f);

		ImGui::SliderFloat("wave_speed", &uniforms.c, 0.f, 12.f);

		if (ImGui::Button("reset")) {
			upload_img_data(dev,swap_img[0], grid_w, grid_h);
		}
	}

	panel_2d.update(dev);
	panel_3d.update(dev);

	uniforms.cursor1 = uniforms.cursor2;
	uniforms.cursor2 = panel_2d.get_world_cursor_pos();
	uniforms.active = 
		app->input.right_mouse_pressed && 
		panel_2d.panel->is_content_selected();

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

void Simulation::render(ev2::Device *dev)
{
	panel_2d.render(dev);
	panel_3d.render(dev);
}

void Simulation::destroy(ev2::Device *dev)
{
	panel_2d.destroy(dev);
	panel_3d.destroy(dev);

	ev2::destroy_descriptor_set(dev, sim0_set);
	ev2::destroy_descriptor_set(dev, sim1_set);

	ev2::destroy_texture(dev, swap_tex[0]);
	ev2::destroy_texture(dev, swap_tex[1]);

	ev2::destroy_image(dev, swap_img[0]);
	ev2::destroy_image(dev, swap_img[1]);
}

int main(int argc, char *argv[])
{
	std::unique_ptr<App> app (new App{
		.win = {
			.width = 1200,
			.height = 500,
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
