#include "app.h"
#include "utils.h"
#include "panel.h"
#include "texture_viewer.h"
#include "heightmap_viewer.h"

#include "poisson_solver.h"
#include "boundary_editor.h"

#include <ev2/utils/log.h>
#include <ev2/utils/common.h>

#include <ev2/context.h>
#include <ev2/pipeline.h>
#include <ev2/resource.h>

#include <ev2/utils/camera.h>
#include <ev2/utils/geometry.h>

// glm
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

// std
#include <memory>
#include <cstdlib>

struct FluidParticle
{
	glm::vec2 pos;
	glm::vec2 vel;
};

struct FluidSim
{
	uint32_t grid_w;
	uint32_t grid_h;

	static constexpr uint32_t DIMS = 2;

	ev2::ImageID v_img_1[DIMS];
	ev2::ImageID v_img_2[DIMS];

	ev2::ImageID lap_p_img; // rhs of lap(phi) = f 
	ev2::ImageID p_img; // pressure

	ev2::ImageID q_img; // density track, arbitrary advected quantity
	ev2::TextureID q_tex[2];
	
	ev2::ImageID mask_img; // out of bounds mask: 0 = oob, 1 = inb

	ev2::TextureID lap_p_tex; // rhs of lap(phi) = f 
	ev2::TextureID p_tex; // pressure
	
	ev2::TextureID mask_tex;
	
	ev2::BufferID ubo;
	ev2::BufferID particles;

	uint32_t particle_count;

	ev2::ComputePipelineID nvs_particles;
	ev2::ComputePipelineID nvs_advect;
	ev2::ComputePipelineID nvs_diffuse;
	ev2::ComputePipelineID nvs_divergence;
	ev2::ComputePipelineID nvs_project;

	ev2::BindingsID base_set;

	ev2::BindingsID particles_set;
	ev2::BindingsID advect_set;
	ev2::BindingsID diffuse_set;
	ev2::BindingsID pressure_set;
	ev2::BindingsID project_set;

	std::unique_ptr<PoissonSolver> pressure_solver;
	std::unique_ptr<MeanSubtractor> mean_subtractor;

	uint64_t step = 0;

	struct Uniforms {
		glm::vec2 cursor = glm::vec2(1,0.5);
		glm::vec2 cursor_prev;
		uint32_t flags;
		float gravity = 0;
	} uniforms;

	int update_advect_set(ev2::GfxContext *ctx);
	int update_diffuse_set(ev2::GfxContext *ctx);
	int update_divergence_set(ev2::GfxContext *ctx);
	int update_project_set(ev2::GfxContext *ctx);

	int init(ev2::GfxContext *ctx, uint32_t w, uint32_t h);
	int update(ev2::GfxContext *ctx);
	void step_sim(ev2::GfxContext *ctx);
	void destroy(ev2::GfxContext *ctx);

	size_t get_particle_buffer_size() {
		return particle_count * sizeof(FluidParticle);
	}
};

int FluidSim::init(ev2::GfxContext *ctx, uint32_t w, uint32_t h)
{
	if (!is_pow2(w) || !is_pow2(h))
		return EXIT_FAILURE;

	grid_w = w;
	grid_h = h;

	particle_count = grid_w * grid_h;

	ev2::ImageUsageFlags usage = 
		ev2::IMAGE_USAGE_STORAGE_BIT | 
		ev2::IMAGE_USAGE_SAMPLED_BIT;

	const glm::ivec2 v_grid_sizes[] = {
		glm::ivec2(1 + grid_w, grid_h),
		glm::ivec2(grid_w, 1 + grid_h),
	};

	for (int i = 0; i < DIMS; ++i) {
		glm::ivec2 size = v_grid_sizes[i];
		v_img_1[i] = ev2::create_image(ctx, size.x, size.y, 1, ev2::IMAGE_FORMAT_32F, usage);
		v_img_2[i] = ev2::create_image(ctx, size.x, size.y, 1, ev2::IMAGE_FORMAT_32F, usage);
	}

	particles = ev2::create_buffer(ctx, particle_count * sizeof(FluidParticle),
		ev2::BUFFER_USAGE_STORAGE_BUFFER_BIT | ev2::BUFFER_USAGE_VERTEX_BUFFER_BIT); 

	q_img = ev2::create_image(ctx, grid_w, grid_h, 2, ev2::IMAGE_FORMAT_32F, usage);
	ev2::set_image_name(ctx, q_img, "q_img");

	q_tex[0] = ev2::create_texture(ctx, q_img, ev2::FILTER_BILINEAR, 0, 0);
	q_tex[1] = ev2::create_texture(ctx, q_img, ev2::FILTER_BILINEAR, 0, 1);

	lap_p_img = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_32F, usage);
	ev2::set_image_name(ctx, lap_p_img, "lap_p_img");

	p_img = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_32F, usage);
	ev2::set_image_name(ctx, p_img, "p_img");

	mask_img = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_R8_UNORM, usage);
	ev2::set_image_name(ctx, mask_img, "bd_mask");

	lap_p_tex = ev2::create_texture(ctx, lap_p_img, ev2::FILTER_BILINEAR);
	p_tex = ev2::create_texture(ctx, p_img, ev2::FILTER_BILINEAR);

	ubo = ev2::create_buffer(ctx, sizeof(uniforms), ev2::BUFFER_USAGE_UNIFORM_BUFFER_BIT);

	pressure_solver.reset(new PoissonSolver);

	int result = EXIT_SUCCESS;

	if ((result = pressure_solver->init(ctx, w, h))) {
		return result;
	}

	mean_subtractor.reset(new MeanSubtractor);

	if ((result = mean_subtractor->init(ctx, grid_w, grid_h))) {
		return result;
	}
	mean_subtractor->setup_bindings(ctx, lap_p_img);

	nvs_particles = ev2::load_compute_pipeline(ctx, "shader/nvs2_particles");
	nvs_advect = ev2::load_compute_pipeline(ctx, "shader/nvs2_advect");
	nvs_diffuse = ev2::load_compute_pipeline(ctx, "shader/nvs2_diffuse");
	nvs_divergence = ev2::load_compute_pipeline(ctx, "shader/nvs2_divergence");
	nvs_project = ev2::load_compute_pipeline(ctx, "shader/nvs2_project");

	particles_set = ev2::create_bindings(ctx, nvs_particles, 1, ev2::BINDING_MODE_STATIC);
	for (int i = 0; i < DIMS; ++i) {
		ev2::bind_image_indexed(ctx, particles_set, "v_in", i, v_img_1[i]);
	}
	ev2::bind_buffer(ctx, particles_set, "Particles", particles, 0, particle_count * sizeof(glm::vec2));
	ev2::flush_bindings(ctx, particles_set);

	update_advect_set(ctx);
	update_diffuse_set(ctx);
	update_divergence_set(ctx);
	update_project_set(ctx);

	base_set = ev2::create_bindings(ctx, nvs_advect, 0, ev2::BINDING_MODE_STATIC);
	ev2::bind_image(ctx, base_set, "bd_mask", mask_img);
	ev2::bind_buffer(ctx, base_set, "ubo", ubo, 0, sizeof(Uniforms));
	ev2::flush_bindings(ctx, base_set);

	return 0;
}

int FluidSim::update_advect_set(ev2::GfxContext *ctx)
{
	ev2::BindingsID set = ev2::create_bindings(ctx, nvs_advect, 1, ev2::BINDING_MODE_STATIC);
	
	for (int i = 0; i < DIMS; ++i) {
		ev2::bind_image_indexed(ctx, set, "v_in", i, v_img_1[i]);
		ev2::bind_image_indexed(ctx, set, "v_out", i, v_img_2[i]);
	}

	for (int i = 0; i < 2; ++i) {
		ev2::bind_texture_indexed(ctx, set, "q_in", i, q_tex[i]); 
		ev2::bind_image_indexed(ctx, set, "q_out", i, q_img, 0, (i + 1) & 0x1); 
	}

	ev2::flush_bindings(ctx, set);

	advect_set = set;

	return 0;
}

int FluidSim::update_diffuse_set(ev2::GfxContext *ctx)
{
	ev2::BindingsID set = ev2::create_bindings(ctx, nvs_diffuse, 1, ev2::BINDING_MODE_STATIC);
	
	for (int i = 0; i < DIMS; ++i) {
		ev2::bind_image_indexed(ctx, set, "v_in", i, v_img_2[i]);
		ev2::bind_image_indexed(ctx, set, "v_out", i, v_img_1[i]);
	}

	ev2::flush_bindings(ctx, set);

	diffuse_set = set;

	return 0;
}

int FluidSim::update_divergence_set(ev2::GfxContext *ctx)
{
	ev2::BindingsID set = ev2::create_bindings(ctx, nvs_divergence, 1, ev2::BINDING_MODE_STATIC);
	
	for (int i = 0; i < DIMS; ++i) {
		ev2::bind_image_indexed(ctx, set, "v_in", i, v_img_1[i]);
	}
	ev2::bind_image(ctx, set, "f_out", lap_p_img);
	ev2::flush_bindings(ctx, set);

	pressure_set = set;

	return 0;
}
int FluidSim::update_project_set(ev2::GfxContext *ctx)
{
	ev2::BindingsID set = ev2::create_bindings(ctx, nvs_project, 1, ev2::BINDING_MODE_STATIC);

	for (int i = 0; i < DIMS; ++i) {
		ev2::bind_image_indexed(ctx, set, "v_out", i, v_img_1[i]);
	}
	ev2::bind_texture(ctx, set, "p_in", p_tex);
	ev2::bind_texture(ctx, set, "lap_p_in", lap_p_tex);
	ev2::flush_bindings(ctx, set);

	project_set = set;

	return 0;
}

void FluidSim::step_sim(ev2::GfxContext *ctx)
{
	uint32_t group_size = 16;

	uint32_t gx = 1 + grid_w/group_size;
	uint32_t gy = 1 + grid_h/group_size;

	ev2::PassID pass = ev2::begin_compute_pass(ctx);
	ev2::cmd_use_image(pass, mask_img, ev2::USAGE_STORAGE_READ_COMPUTE);
	ev2::cmd_use_image(pass, q_img, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);

	ev2::cmd_use_buffer(pass, ubo, ev2::USAGE_UNIFORM_READ);

	for (int i = 0; i < DIMS; ++i) {
		ev2::cmd_use_image(pass, v_img_1[i], ev2::USAGE_SAMPLED_COMPUTE);
	}

	struct {
		uint32_t count;
		uint32_t step;
	} pc_particle = {
		.count = particle_count,
		.step = (uint32_t)step
	};

	ev2::cmd_use_buffer(pass, particles, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
	ev2::cmd_bind_compute_pipeline(pass, nvs_particles);
	ev2::cmd_push_constant(pass, nvs_particles, 0, sizeof(pc_particle), &pc_particle);
	ev2::cmd_bind_resources(pass, base_set);
	ev2::cmd_bind_resources(pass, particles_set);
	ev2::cmd_dispatch(pass, 1 + (particle_count - 1)/32, 1, 1);

	for (int i = 0; i < DIMS; ++i) {
		ev2::cmd_use_image(pass, v_img_2[i], ev2::USAGE_STORAGE_WRITE_COMPUTE);
	}

	struct {
		uint32_t q_img_idx;
	} pc_advect = {
		.q_img_idx = (uint32_t)(step & 0x1),
	};

	ev2::cmd_bind_compute_pipeline(pass, nvs_advect);
	ev2::cmd_push_constant(pass, nvs_advect, 0, sizeof(pc_advect), &pc_advect);

	ev2::cmd_bind_resources(pass, base_set);
	ev2::cmd_bind_resources(pass, advect_set);
	ev2::cmd_dispatch(pass, gx, gy, 1);

	for (int i = 0; i < DIMS; ++i) {
		ev2::cmd_use_image(pass, v_img_1[i], ev2::USAGE_STORAGE_WRITE_COMPUTE);
		ev2::cmd_use_image(pass, v_img_2[i], ev2::USAGE_STORAGE_READ_COMPUTE);
	}

	ev2::cmd_bind_compute_pipeline(pass, nvs_diffuse);
	ev2::cmd_bind_resources(pass, base_set);
	ev2::cmd_bind_resources(pass, diffuse_set);
	ev2::cmd_dispatch(pass, gx, gy, 1);

	for (int i = 0; i < DIMS; ++i) {
		ev2::cmd_use_image(pass, v_img_1[i], ev2::USAGE_SAMPLED_COMPUTE);
	}
	ev2::cmd_use_image(pass, lap_p_img, ev2::USAGE_STORAGE_WRITE_COMPUTE);

	ev2::cmd_bind_compute_pipeline(pass, nvs_divergence);
	ev2::cmd_bind_resources(pass, base_set);
	ev2::cmd_bind_resources(pass, pressure_set);
	ev2::cmd_dispatch(pass, gx, gy, 1);

	//mean_subtractor->record(pass);
	
	pressure_solver->record_setup(pass);
	for (int i = 0; i < ((step == 0) ? 64 : 2); ++i) 
		pressure_solver->record_v_cycle(pass);

	for (int i = 0; i < DIMS; ++i) {
		ev2::cmd_use_image(pass, v_img_1[i], ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
	}

	ev2::cmd_use_image(pass, p_img, ev2::USAGE_SAMPLED_COMPUTE);
	ev2::cmd_use_image(pass, lap_p_img, ev2::USAGE_SAMPLED_COMPUTE);

	ev2::cmd_bind_compute_pipeline(pass, nvs_project);
	ev2::cmd_bind_resources(pass, base_set);
	ev2::cmd_bind_resources(pass, project_set);
	ev2::cmd_dispatch(pass, gx, gy, 1);

	//mean_subtractor->set_image(ctx, p_img);
	//mean_subtractor->record(rec);

	ev2::end_pass(ctx, pass);

	 ++step;
}

int FluidSim::update(ev2::GfxContext *ctx)
{
	ev2::UploadContext uc = ev2::begin_upload(ctx, sizeof(Uniforms), alignof(Uniforms));
	memcpy(uc.ptr, &uniforms, sizeof(Uniforms));
	ev2::BufferUpload up = {.size = sizeof(Uniforms)};
	uint64_t sync = ev2::commit_buffer_uploads(ctx, uc, ubo, &up, 1);
	ev2::flush_uploads(ctx);

	pressure_solver->set_inputs(ctx, p_img, lap_p_img, mask_img);

	return 0;
}
void FluidSim::destroy(ev2::GfxContext *ctx)
{
	pressure_solver->destroy(ctx);
	mean_subtractor->destroy(ctx);

	for (int i = 0; i < DIMS; ++i) {
		ev2::destroy_image(ctx, v_img_1[i]);
		ev2::destroy_image(ctx, v_img_2[i]);
	}

	ev2::destroy_image(ctx, lap_p_img);
	ev2::destroy_image(ctx, p_img);
	
	//ev2::destroy_texture(ctx, q_tex_1);
	//ev2::destroy_texture(ctx, q_tex_2);

	ev2::destroy_texture(ctx, lap_p_tex);
	ev2::destroy_texture(ctx, p_tex);
	
	ev2::destroy_buffer(ctx, ubo);

	ev2::destroy_bindings(ctx, advect_set);
	ev2::destroy_bindings(ctx, diffuse_set);
	ev2::destroy_bindings(ctx, pressure_set);
	ev2::destroy_bindings(ctx, project_set);
}

struct FluidApp : public App
{
	std::unique_ptr<FluidSim> sim;
	std::unique_ptr<ImageViewerPanel> main_panel;
	std::unique_ptr<ImageViewerPanel> right_panel;
	std::unique_ptr<BoundaryEditor> boundary_editor;
	std::unique_ptr<HeightmapViewerPanel> heightmap_panel;

	ev2::TextureID v_tex[FluidSim::DIMS];

	ev2::TextureID phi_tex;
	ev2::TextureID f_tex;

	ev2::BindingsID particle_bindings;
	ev2::GfxPipelineID particles;

	bool b_enable_right_panel = false;
	bool b_heightmap_panel = true;
	bool b_main_panel = true;
	bool b_enable_flux_arrows = false;

	bool m_stopped = false;
	uint64_t m_step = 0;
	float m_rate = 1.f;

	FluidApp() : App(1200, 1200, "fluid") {
	}

	int initialize(int argc, char **argv);
	int update();
	void render();
	void destroy();

	void reset_images();
};

int FluidApp::initialize(int argc, char **argv)
{
	int result = App::initialize(argc, argv);
	if (result)
		return result;

	sim.reset(new FluidSim);

	main_panel.reset(new ImageViewerPanel(this, 200, 0, 500, 500,
		"pipelines/fluid_viz.yaml", "Interactive Simulation"));
	right_panel.reset(new ImageViewerPanel(this, 700, 0, 500, 500, 
		"pipelines/pressure_viz.yaml", "Pressure Debug"));

	boundary_editor.reset(new BoundaryEditor(this, 100, 100, 500, 500, "Boundary Mask"));

	heightmap_panel.reset(new HeightmapViewerPanel());

	result = sim->init(ctx, 512, 512);
	if (result)
		return result;

	for (int i = 0; i < FluidSim::DIMS; ++i) {
		v_tex[i] = ev2::create_texture(ctx, sim->v_img_1[i], ev2::FILTER_BILINEAR);
	}

	result = main_panel->init(ctx, sim->q_img); 
	if (result)
		return result;
	main_panel->panel->set_closable(false);

	result = right_panel->init(ctx, sim->p_img); 
	if (result)
		return result;
	right_panel->panel->set_closable(false);

	result = heightmap_panel->init(this, ctx, sim->p_tex); 
	if(result)
		return result;
	heightmap_panel->panel->set_closable(false);

	result = boundary_editor->init(ctx, sim->mask_img); 
	if(result)
		return result;
	boundary_editor->panel->set_closable(false);

	reset_images();

	particles = ev2::load_graphics_pipeline(ctx, "pipelines/pde/fluid_particles.yaml");
	particle_bindings = ev2::create_bindings(ctx, particles, EV2_GFX_SET_PER_DRAW, ev2::BINDING_MODE_DYNAMIC);

	return result;
}

void FluidApp::reset_images()
{
	initialize_image(ctx, sim->p_img, 0.f);

	for (int i = 0; i < FluidSim::DIMS; ++i) {
		initialize_image(ctx, sim->v_img_1[i], glm::vec4(0));
		initialize_image(ctx, sim->v_img_2[i], glm::vec4(0));
	}

	initialize_image<float>(ctx, sim->q_img, 0.f, 0);
	initialize_image<float>(ctx, sim->q_img, 0.f, 1);

	initialize_image<uint8_t>(ctx, sim->mask_img, UINT8_MAX);

	ev2::flush_uploads(ctx);

	sim->uniforms.cursor = sim->uniforms.cursor_prev = glm::vec2(1,0.5);
	sim->step = 0;
}

int FluidApp::update()
{
	int result = EXIT_SUCCESS;
	uint64_t current_step = m_step;

	ImGui::Begin("Editor");

	if (ImGui::Checkbox("Stopped", &m_stopped)) {
	}

	if (ImGui::RadioButton("Enable flux arrows", b_enable_flux_arrows)) {
		b_enable_flux_arrows = !b_enable_flux_arrows;
	}

	if (ImGui::Button("Step")) {
		++m_step;
	} else if (!m_stopped) {
		++m_step;
	}

	ImGui::SliderFloat("Sim update rate", &m_rate, 1.f/256.f, 1.f);

	if (ImGui::Button("Reset")) {
		reset_images();
	}

	ImGui::SliderFloat("gravity", &sim->uniforms.gravity, -1, 1);

	ImGui::End();

	int skip = std::max((int)(1/m_rate), 1);

	if (m_step % skip == 0 && current_step != m_step) {
		if ((result = sim->update(ctx)))
			return result;

		for (int i = 0; i < 3; ++i) {
			sim->step_sim(ctx);
		}
	}
	result = main_panel->update(ctx);
	if (result < App::OK)
		return result;

	result = right_panel->update(ctx);
	if (result < App::OK)
		return result;

	result = boundary_editor->update(ctx);
	if (result < App::OK)
		return result;
	
	result = heightmap_panel->update(ctx);
	if (result < App::OK)
		return result;

	bool is_panel_clicked = this->input.right_mouse_pressed && 
			main_panel->panel->is_content_selected();

	if (is_panel_clicked) {
		sim->uniforms.cursor_prev = sim->uniforms.cursor;
		sim->uniforms.cursor = main_panel->get_world_cursor_pos();
		sim->uniforms.flags = true; 
	} else {
		sim->uniforms.flags = false; 
	}

	return result;
}
void FluidApp::render()
{
	// First render base image into the main panel
	main_panel->render(ctx);

	// second pass renders with alpha blending
	ev2::GfxPassInfo pass_info = {
		.target = main_panel->panel->get_target(),
		.view = main_panel->rd.camera,
		.clear_color = false,
	};
	ev2::PassID pass = ev2::begin_gfx_pass(ctx, &pass_info);

	if (b_enable_flux_arrows) {
		ev2::GfxPipelineID flux_arrows = ev2::load_graphics_pipeline(ctx, "pipelines/flux.yaml");
		
		for (int i = 0; i < FluidSim::DIMS; ++i) {
			ev2::cmd_use_image(pass, sim->v_img_1[i], ev2::USAGE_SAMPLED_GRAPHICS);
		}

		struct alignas(8) {
			glm::ivec2 size;
			uint32_t v_img[2];
		} pc = {
			.size = glm::ivec2(sim->grid_w, sim->grid_h),
			.v_img = {
				ev2::get_bindless_handle(ctx, v_tex[0]),
				ev2::get_bindless_handle(ctx, v_tex[1])
			},
		};

		ev2::cmd_bind_gfx_pipeline(pass, flux_arrows);
		ev2::cmd_push_constant(pass, flux_arrows, 0, sizeof(pc), &pc);
		ev2::cmd_custom(pass, [w = pc.size.x, h = pc.size.y]
		(VkCommandBuffer cmds) {
			uint32_t count = (1 + w) * h + w * (1 + h);
			vkCmdDraw(cmds, 9, count, 0, 0);
		});
	}

	ev2::reset_bindings(ctx, particle_bindings);
	ev2::bind_buffer(ctx, particle_bindings, "Particles", sim->particles, 0, sim->particle_count * sizeof(glm::vec2));
	ev2::flush_bindings(ctx, particle_bindings);

	struct {
		glm::mat2 world;
	} pc {
		.world = glm::mat3x2(
			glm::vec2(2.f/(float)sim->grid_w, 0),
			glm::vec2(0, 2.f/(float)sim->grid_h),
			glm::vec2(-1, -1)
		)
	};

	ev2::cmd_bind_gfx_pipeline(pass, particles);
	ev2::cmd_bind_resources(pass, particle_bindings);
	ev2::cmd_push_constant(pass, particles, 0, sizeof(pc), &pc);
	ev2::cmd_custom(pass, [this](VkCommandBuffer cmds){
		vkCmdDraw(cmds, 6, sim->particle_count, 0, 0);
	});

	ev2::end_pass(ctx, pass);

	right_panel->render(ctx);
	boundary_editor->render(ctx);
	heightmap_panel->render(ctx);
}
void FluidApp::destroy()
{
	main_panel->destroy(ctx);
	right_panel->destroy(ctx);
	boundary_editor->destroy(ctx);
	heightmap_panel->destroy(ctx);

	sim->destroy(ctx);

	App::terminate();
}

int main(int argc, char *argv[])
{
	std::unique_ptr<FluidApp> app (new FluidApp{});

	if (app->initialize(argc, argv) != App::OK)
		return EXIT_FAILURE;

	int status = App::OK;

	for(;;)
	{
		status = app->begin_frame();
		if (should_exit(status))
			break;

		status = app->update();
		if (should_exit(status))
			break;
		
		app->render();

		status = app->end_frame();
		if (should_exit(status))
			break;
	}

	app->destroy();

	return EXIT_SUCCESS;
}
