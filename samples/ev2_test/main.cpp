#include "app.h"
#include "panel.h"
#include "texture_viewer.h"
#include "heightmap_viewer.h"

#include <ev2/utils/log.h>

#include <ev2/context.h>
#include <ev2/resource.h>

#include <ev2/utils/camera.h>
#include <ev2/utils/geometry.h>

// glm
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

// std
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cmath>

uint64_t upload_img_data(ev2::GfxContext *ctx, ev2::ImageID img, 
					 uint32_t w, uint32_t h)
{
	size_t size = w * h * sizeof(glm::vec4);
	ev2::UploadContext uc = ev2::begin_upload(ctx, size, alignof(glm::vec4));

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

	return ev2::commit_image_uploads(ctx, uc, img, &upload, 1);
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

	ev2::BindingsID sim0_bindings;
	ev2::BindingsID sim1_bindings;

	int swap_ctr = 0;

	ev2::BindingSlot img_in_slot;
	ev2::BindingSlot img_out_slot; 

	int init(ev2::GfxContext *ctx);
	int update(ev2::GfxContext *ctx);
	void destroy(ev2::GfxContext *ctx);
};

//------------------------------------------------------------------------------
// Simulation

int WaveSim::init(ev2::GfxContext *ctx)
{
	sim_pipelines[0] = ev2::load_compute_pipeline(ctx, 
		"shader/pde0.comp.spv");

	if (!EV2_VALID(sim_pipelines[0]))
		return EXIT_FAILURE;

	sim_pipelines[1] = ev2::load_compute_pipeline(ctx, 
		"shader/pde1.comp.spv");

	if (!EV2_VALID(sim_pipelines[1]))
		return EXIT_FAILURE;

	grid_w = 512, grid_h = grid_w;

	swap_img[0] = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_RGBA32F,
								 ev2::IMAGE_USAGE_STORAGE_BIT | ev2::IMAGE_USAGE_SAMPLED_BIT);
	swap_img[1] = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_RGBA32F,
								 ev2::IMAGE_USAGE_STORAGE_BIT | ev2::IMAGE_USAGE_SAMPLED_BIT);

	swap_tex[0] = ev2::create_texture(ctx, swap_img[0], ev2::FILTER_BILINEAR);
	swap_tex[1] = ev2::create_texture(ctx, swap_img[1], ev2::FILTER_BILINEAR);

	ubo = ev2::create_buffer(ctx, sizeof(uniforms),
		ev2::BUFFER_USAGE_TRANSFER_DST_BIT | ev2::BUFFER_USAGE_UNIFORM_BUFFER_BIT);

	//------------------------------------------------------------------------------
	// Get shader resource locations and create descriptor sets

	sim0_bindings = ev2::create_bindings(ctx, sim_pipelines[0], 0, ev2::BINDING_MODE_DYNAMIC);
	sim1_bindings = ev2::create_bindings(ctx, sim_pipelines[0], 0, ev2::BINDING_MODE_DYNAMIC);

	//------------------------------------------------------------------------------
	// Upload some stuff

	uint64_t sync = upload_img_data(ctx,swap_img[0], grid_w, grid_h);

	return EXIT_SUCCESS;
}

int WaveSim::update(ev2::GfxContext *ctx)
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
			sync = upload_img_data(ctx,swap_img[0], grid_w, grid_h);
		}
	}

	ImGui::End();
	ev2::UploadContext uc = ev2::begin_upload(ctx,
		sizeof(uniforms), alignof(decltype(uniforms)));

	memcpy(uc.ptr, &uniforms, sizeof(uniforms));
	ev2::BufferUpload upload = {
		.src_offset = 0,
		.dst_offset = 0,
		.size = sizeof(uniforms),
	};

	sync = ev2::commit_buffer_uploads(ctx, uc, ubo, &upload, 1);

	int img_A = 0;
	int img_B = 1;

	//-------------------------------------------------------------------
	// Simulation

	uint32_t grps_x = grid_w/32, grps_y = grid_h/32, grps_z = 1;

	ev2::reset_bindings(ctx, sim0_bindings);
	ev2::bind_buffer(ctx, sim0_bindings, "Uniforms", ubo, 0, sizeof(uniforms));
	ev2::bind_texture(ctx, sim0_bindings, "img_in", swap_tex[img_A]);
	ev2::bind_image(ctx, sim0_bindings, "img_out", swap_img[img_B]);
	ev2::flush_bindings(ctx, sim0_bindings);

	ev2::reset_bindings(ctx, sim1_bindings);
	ev2::bind_buffer(ctx, sim1_bindings, "Uniforms", ubo, 0, sizeof(uniforms));
	ev2::bind_texture(ctx, sim1_bindings, "img_in", swap_tex[img_B]);
	ev2::bind_image(ctx, sim1_bindings, "img_out", swap_img[img_A]);
	ev2::flush_bindings(ctx, sim1_bindings);

	ev2::PassID pass = ev2::begin_compute_pass(ctx);

	ev2::cmd_use_image(pass, swap_img[img_A], ev2::USAGE_STORAGE_READ_COMPUTE);
	ev2::cmd_use_image(pass, swap_img[img_B], ev2::USAGE_STORAGE_RW_COMPUTE);

	ev2::cmd_bind_resources(pass, sim0_bindings);
	ev2::cmd_bind_compute_pipeline(pass, sim_pipelines[0]);
	ev2::cmd_dispatch(pass, grps_x, grps_y, grps_z);

	ev2::cmd_use_image(pass, swap_img[img_B], ev2::USAGE_STORAGE_READ_COMPUTE);
	ev2::cmd_use_image(pass, swap_img[img_A], ev2::USAGE_STORAGE_RW_COMPUTE);

	ev2::cmd_bind_resources(pass, sim1_bindings);
	ev2::cmd_bind_compute_pipeline(pass, sim_pipelines[1]);
	ev2::cmd_dispatch(pass, grps_x, grps_y, grps_z);

	ev2::cmd_use_image(pass, swap_img[img_B], ev2::USAGE_SAMPLED_GRAPHICS);

	ev2::end_pass(ctx, pass);

	return App::OK;
}

void WaveSim::destroy(ev2::GfxContext *ctx)
{
	ev2::destroy_bindings(ctx, sim0_bindings);
	ev2::destroy_bindings(ctx, sim1_bindings);

	ev2::destroy_texture(ctx, swap_tex[0]);
	ev2::destroy_texture(ctx, swap_tex[1]);

	ev2::destroy_image(ctx, swap_img[0]);
	ev2::destroy_image(ctx, swap_img[1]);
}

struct FluidApp : public App
{
	std::unique_ptr<WaveSim> sim;
	std::unique_ptr<TextureViewerPanel> main_panel;
	std::unique_ptr<HeightmapViewerPanel> heightmap_panel;

	ev2::TextureID phi_tex;
	ev2::TextureID f_tex;

	struct Uniforms {
		glm::vec2 cursor1;
		glm::vec2 cursor2;
		uint32_t flags;
	} uniforms;
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

	main_panel.reset(new TextureViewerPanel(this, 0, 0, 500, 500));
	heightmap_panel.reset(new HeightmapViewerPanel);

	result = sim->init(ctx);
	if (result)
		return result;

	result = main_panel->init(ctx, sim->swap_tex[1]); 
	if (result)
		return result;

	result = heightmap_panel->init(this, ctx, sim->swap_tex[1]); 
	if (result)
		return result;


	return result;
}
int FluidApp::update()
{
	int result = EXIT_SUCCESS;

	if ((result = App::update()))
		return result;

	if ((result = main_panel->update(ctx)))
		return result;

	if ((result = heightmap_panel->update(ctx)))
		return result;

	if ((result = sim->update(ctx)))
		return result;

	sim->uniforms.cursor1 = sim->uniforms.cursor2;
	sim->uniforms.cursor2 = main_panel->get_world_cursor_pos();
	sim->uniforms.active = 
		this->input.right_mouse_pressed && 
		main_panel->panel->is_content_selected();

	ev2::flush_uploads(ctx);

	return result;
}
void FluidApp::render()
{
	main_panel->render(ctx);
	heightmap_panel->render(ctx);
}
void FluidApp::destroy()
{
	heightmap_panel->destroy(ctx);
	main_panel->destroy(ctx);
	sim->destroy(ctx);

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
		app->render();
		if (app->end_frame() != ev2::SUCCESS)
			return EXIT_FAILURE;
	}

	app->destroy();

	return EXIT_SUCCESS;
}
