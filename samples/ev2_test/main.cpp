#include "app.h"
#include "panel.h"
#include "texture_viewer.h"
#include "heightmap_viewer.h"

#include <engine/utils/log.h>

#include <ev2/device.h>
#include <ev2/render.h>
#include <ev2/resource.h>

#include <engine/utils/camera.h>
#include <engine/utils/geometry.h>

// glm
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

// std
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cmath>

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

	grid_w = 512, grid_h = grid_w;

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

struct FluidApp : public App
{
	std::unique_ptr<WaveSim> sim;
	std::unique_ptr<TextureViewerPanel> f_panel;
	std::unique_ptr<HeightmapViewerPanel> heightmap_panel;

	ev2::TextureID phi_tex;
	ev2::TextureID f_tex;

	struct Uniforms {
		glm::vec2 cursor1;
		glm::vec2 cursor2;
		uint32_t flags;
	} uniforms;
	ev2::ComputePipelineID cursor;
	ev2::DescriptorSetID cursor_desc;
	ev2::BufferID ubo;

	FluidApp() : App(1200, 500, "fluid") {
	}

	int initialize(int argc, char **argv);
	int update();
	void render();
	void destroy();
};

int FluidApp::initialize(int argc, char **argv)
{
	int result = App::initialize(argc, argv);
	if (result)
		return result;

	sim.reset(new WaveSim);

	f_panel.reset(new TextureViewerPanel(this, 0, 0, 500, 500));
	heightmap_panel.reset(new HeightmapViewerPanel);

	result = sim->init(dev);
	if (result)
		return result;

	result = f_panel->init(dev, sim->swap_tex[1]); 
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

	if ((result = f_panel->update(dev)))
		return result;

	if ((result = heightmap_panel->update(dev)))
		return result;

	if ((result = sim->update(dev)))
		return result;

	sim->uniforms.cursor1 = sim->uniforms.cursor2;
	sim->uniforms.cursor2 = f_panel->get_world_cursor_pos();
	sim->uniforms.active = 
		this->input.right_mouse_pressed && 
		f_panel->panel->is_content_selected();

	return result;
}
void FluidApp::render()
{
	f_panel->render(dev);
	heightmap_panel->render(dev);
}
void FluidApp::destroy()
{
	heightmap_panel->destroy(dev);
	f_panel->destroy(dev);
	sim->destroy(dev);

	App::terminate();
}

int main(int argc, char *argv[])
{
	std::unique_ptr<FluidApp> app (new FluidApp{});

	if (app->initialize(argc, argv) != App::OK)
		return EXIT_FAILURE;

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
