#include "app.h"
#include "utils.h"
#include "panel.h"
#include "texture_viewer.h"
#include "heightmap_viewer.h"

#include "poisson_solver.h"
#include "boundary_editor.h"
#include "grid_sim.h"

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

struct VelocityCell
{
	float acc;
	float wt;
};

struct SimParams
{
	glm::vec2 cursor1;
	glm::vec2 cursor2;
	uint cursor_flags;
};

struct FLIPFluidSim
{
	uint32_t grid_w;
	uint32_t grid_h;

	static constexpr uint32_t DIMS = 2;

	ev2::ImageID v_pre_proj_img[DIMS];
	ev2::ImageID v_proj_img[DIMS];
	ev2::BufferID deposit_buf;

	ev2::ImageID lap_p_img; // rhs of lap(phi) = f 
	ev2::ImageID p_img; // pressure
	
	// solid mask: 
	// 0 = solid, 
	// 1 = free space
	ev2::ImageID solid_mask_img;

	// boundary mask: 
	// x < 0 -> air, 
	// 0 < x < 1 -> solid,
	// x = 1 -> fluid
	ev2::ImageID bd_mask_img;
	
	ev2::BufferID part_data;

	uint32_t particle_count;

	// shared descriptor set across all stages
	ev2::BindingsID bindings;

	// correct velocity using projected grid and apply
	// lagrangian advection step
	ev2::ComputePipelineID p_advect;
	// deposit velocities back into a buffer
	ev2::ComputePipelineID p_deposit;
	// compute divergence
	ev2::ComputePipelineID p_divergence;
	// after solving for pressure, correct the deposited velocities
	ev2::ComputePipelineID p_project;

	std::unique_ptr<PoissonSolver> pressure_solver;
	std::unique_ptr<MeanSubtractor> mean_subtractor;

	uint64_t step = 0;

	size_t get_depost_buf_header_size() 
	{
		return DIMS * sizeof(uint32_t);
	}
	size_t get_depost_buf_data_size() 
	{
		return ((1 + grid_w) * (grid_h) + (grid_w) * (1 + grid_h)) * sizeof(VelocityCell); 
	}

	int init(ev2::GfxContext *ctx, uint32_t w, uint32_t h)
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
			char buf[100];

			v_pre_proj_img[i] = ev2::create_image(ctx, size.x, size.y, 1, ev2::IMAGE_FORMAT_32F, usage);

			snprintf(buf, sizeof(buf), "v_pre_proj_%d", i);
			ev2::set_image_name(ctx, v_pre_proj_img[i], buf);

			v_proj_img[i] = ev2::create_image(ctx, size.x, size.y, 1, ev2::IMAGE_FORMAT_32F, usage);

			snprintf(buf, sizeof(buf), "v_proj_%d", i);
			ev2::set_image_name(ctx, v_proj_img[i], buf);
		}

		size_t deposit_buf_size = get_depost_buf_header_size() + get_depost_buf_data_size(); 

		deposit_buf = ev2::create_buffer(ctx, deposit_buf_size,
			ev2::BUFFER_USAGE_STORAGE_BUFFER_BIT); 

		part_data = ev2::create_buffer(ctx, particle_count * (sizeof(FluidParticle)),
			ev2::BUFFER_USAGE_STORAGE_BUFFER_BIT | ev2::BUFFER_USAGE_VERTEX_BUFFER_BIT); 

		lap_p_img = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_32F, usage);
		ev2::set_image_name(ctx, lap_p_img, "lap_p_img");

		p_img = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_32F, usage);
		ev2::set_image_name(ctx, p_img, "p_img");

		bd_mask_img = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_R8_SNORM, usage);
		ev2::set_image_name(ctx, bd_mask_img, "bd_mask");

		solid_mask_img = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_R8_UNORM, usage);
		ev2::set_image_name(ctx, solid_mask_img, "solid_mask");

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

		p_advect = ev2::load_compute_pipeline(ctx, "shader/fluid/nvs2_flip_advect");
		p_deposit = ev2::load_compute_pipeline(ctx, "shader/fluid/nvs2_flip_deposit");
		p_divergence = ev2::load_compute_pipeline(ctx, "shader/fluid/nvs2_flip_divergence");
		p_project = ev2::load_compute_pipeline(ctx, "shader/fluid/nvs2_flip_project");

		bindings = ev2::create_bindings(ctx, p_advect, 0, ev2::BINDING_MODE_STATIC);

		ev2::bind_image(ctx, bindings, "solid_mask", solid_mask_img);
		ev2::bind_image(ctx, bindings, "bd_mask", bd_mask_img);

		for (int i = 0; i < DIMS; ++i) {
			ev2::bind_image_indexed(ctx, bindings, "v_pre_proj", i, v_pre_proj_img[i]);
			ev2::bind_image_indexed(ctx, bindings, "v_proj", i, v_proj_img[i]);
		}

		ev2::bind_buffer(ctx, bindings, "Particles", part_data, 
			0, particle_count * sizeof(FluidParticle)); 
		ev2::bind_buffer(ctx, bindings, "VelocityBuffer", deposit_buf, 0, deposit_buf_size);  

		ev2::bind_image(ctx, bindings, "f_out", lap_p_img);
		ev2::bind_image(ctx, bindings, "p_in", p_img);

		ev2::flush_bindings(ctx, bindings);

		return 0;
	}

	int update(ev2::GfxContext *ctx)
	{
		pressure_solver->set_inputs(ctx, p_img, lap_p_img, bd_mask_img);
		return 0;
	}

	void step_sim(ev2::GfxContext *ctx, const SimParams &params)
	{
		uint32_t group_size = 16;

		uint32_t gx = 1 + grid_w/group_size;
		uint32_t gy = 1 + grid_h/group_size;

		ev2::PassID pass = ev2::begin_compute_pass(ctx);

		struct alignas(sizeof(glm::vec2)) {
			uint32_t count;
			uint32_t step;
			glm::vec2 cursor1;
			glm::vec2 cursor2;
			uint cursor_flags;
		} pc_particle = {
			.count = particle_count,
			.step = (uint32_t)step,
			.cursor1 = params.cursor1,
			.cursor2 = params.cursor2,
			.cursor_flags = params.cursor_flags
		};

		//------------------------------------------------------------------------------
		// velocity projection

		ev2::cmd_push_constant(pass, p_advect, 0, sizeof(pc_particle), &pc_particle);
		ev2::cmd_bind_resources(pass, bindings);

		ev2::cmd_use_buffer(pass, deposit_buf, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
		ev2::cmd_use_image(pass, bd_mask_img, ev2::USAGE_STORAGE_WRITE_COMPUTE);
		for (int i = 0; i < DIMS; ++i) {
			ev2::cmd_use_image(pass, v_pre_proj_img[i], ev2::USAGE_STORAGE_WRITE_COMPUTE);
			ev2::cmd_use_image(pass, v_proj_img[i], ev2::USAGE_STORAGE_WRITE_COMPUTE);
		}
		ev2::cmd_use_image(pass, p_img, ev2::USAGE_SAMPLED_COMPUTE);

		ev2::cmd_bind_compute_pipeline(pass, p_project);
		ev2::cmd_dispatch(pass, gx, gy, 1);

		//------------------------------------------------------------------------------
		// advection stage

		for (int i = 0; i < DIMS; ++i) {
			ev2::cmd_use_image(pass, v_pre_proj_img[i], ev2::USAGE_STORAGE_READ_COMPUTE);
			ev2::cmd_use_image(pass, v_proj_img[i], ev2::USAGE_STORAGE_READ_COMPUTE);
		}
		ev2::cmd_use_buffer(pass, part_data, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);

		ev2::cmd_bind_compute_pipeline(pass, p_advect);
		ev2::cmd_dispatch(pass, 1 + (particle_count - 1)/32, 1, 1);

		//------------------------------------------------------------------------------
		// scatter/deposit stage

		ev2::cmd_use_buffer(pass, part_data, ev2::USAGE_STORAGE_READ_COMPUTE);
		ev2::cmd_use_buffer(pass, deposit_buf, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
		ev2::cmd_use_image(pass, solid_mask_img, ev2::USAGE_STORAGE_READ_COMPUTE);
		ev2::cmd_use_image(pass, bd_mask_img, ev2::USAGE_STORAGE_WRITE_COMPUTE);

		ev2::cmd_bind_compute_pipeline(pass, p_deposit);
		ev2::cmd_dispatch(pass, 1 + (particle_count - 1)/128, 1, 1);

		//------------------------------------------------------------------------------
		// divergence stage

		ev2::cmd_use_buffer(pass, deposit_buf, ev2::USAGE_STORAGE_READ_COMPUTE);
		ev2::cmd_use_image(pass, lap_p_img, ev2::USAGE_STORAGE_WRITE_COMPUTE);
		ev2::cmd_use_image(pass, bd_mask_img, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);

		ev2::cmd_bind_compute_pipeline(pass, p_divergence);
		ev2::cmd_dispatch(pass, gx, gy, 1);

		//------------------------------------------------------------------------------
		// pressure_solve

		pressure_solver->record_setup(pass);
		for (int i = 0; i < ((step == 0) ? 32 : 4); ++i) 
			pressure_solver->record_v_cycle(pass);

		ev2::end_pass(ctx, pass);

		 ++step;
	}

	void reset(ev2::GfxContext *ctx)
	{
		initialize_image(ctx, p_img, 0.f);

		for (int i = 0; i < GridFluidSim::DIMS; ++i) {
			initialize_image(ctx, v_proj_img[i], glm::vec4(0));
			initialize_image(ctx, v_pre_proj_img[i], glm::vec4(0));
		}

		initialize_image<uint8_t>(ctx, bd_mask_img, UINT8_MAX); // -1
		initialize_image<uint8_t>(ctx, solid_mask_img, UINT8_MAX);

		size_t header = get_depost_buf_header_size();
		size_t bufsize = get_depost_buf_data_size();

		ev2::UploadContext uc = ev2::begin_upload(ctx, header + bufsize, alignof(VelocityCell));

		uint32_t grid[DIMS] = {grid_w, grid_h};
		uint32_t offsets[DIMS] = {};

		uint32_t sum = 0;
		for (int i = 0; i < DIMS; ++i) {
			offsets[i] = sum;

			uint32_t count = 1;
			for (int j = 0; j < DIMS; ++j) {
				count *= (i == j) ? grid[j] + 1 : grid[j];
			}

			sum += count;
		}

		assert(sizeof(offsets) == header);

		memcpy(uc.ptr, &offsets, sizeof(offsets));
		memset((char*)uc.ptr + header, 0x0, bufsize);

		ev2::BufferUpload up = {
			.src_offset = 0,
			.dst_offset = 0,
			.size = header + bufsize,
		};
		ev2::commit_buffer_uploads(ctx, uc, deposit_buf, &up, 1);

		ev2::flush_uploads(ctx);

		step = 0;
	}

	void destroy(ev2::GfxContext *ctx)
	{
		for (int i = 0; i < DIMS; ++i) {
			ev2::destroy_image(ctx, v_pre_proj_img[i]);
			ev2::destroy_image(ctx, v_proj_img[i]);
		}
		ev2::destroy_buffer(ctx, deposit_buf);

		ev2::destroy_image(ctx, lap_p_img);
		ev2::destroy_image(ctx, p_img);
		ev2::destroy_image(ctx, solid_mask_img);
		ev2::destroy_image(ctx, bd_mask_img);

		ev2::destroy_buffer(ctx, part_data);
		ev2::destroy_bindings(ctx, bindings);
	}
};

struct FluidApp : public App
{
	std::unique_ptr<FLIPFluidSim> sim;
	std::unique_ptr<ImageViewerPanel> main_panel;
	std::unique_ptr<ImageViewerPanel> right_panel;
	std::unique_ptr<BoundaryEditor> boundary_editor;
	std::unique_ptr<HeightmapViewerPanel> heightmap_panel;

	SimParams params {};

	ev2::TextureID v_tex[GridFluidSim::DIMS];

	ev2::TextureID phi_tex;
	ev2::TextureID f_tex;

	ev2::BindingsID particle_bindings;
	ev2::GfxPipelineID particles;

	bool b_enable_right_panel = false;
	bool b_heightmap_panel = true;
	bool b_main_panel = true;
	bool b_enable_flux_arrows = false;

	bool m_stopped = true;
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

	sim.reset(new FLIPFluidSim);

	main_panel.reset(new ImageViewerPanel(this, 200, 0, 500, 500,
		"pipelines/fluid_viz.yaml", "Interactive Simulation"));
	right_panel.reset(new ImageViewerPanel(this, 700, 0, 500, 500, 
		"pipelines/pressure_viz.yaml", "Pressure Debug"));

	boundary_editor.reset(new BoundaryEditor(this, 100, 100, 500, 500, "Boundary Mask"));

	heightmap_panel.reset(new HeightmapViewerPanel());

	result = sim->init(ctx, 256, 256);
	if (result)
		return result;

	phi_tex = ev2::create_texture(ctx, sim->p_img, ev2::FILTER_NEAREST);

	for (int i = 0; i < GridFluidSim::DIMS; ++i) {
		v_tex[i] = ev2::create_texture(ctx, sim->v_proj_img[i], ev2::FILTER_BILINEAR);
	}

	result = main_panel->init(ctx, sim->lap_p_img); 
	if (result)
		return result;
	main_panel->panel->set_closable(false);

	result = right_panel->init(ctx, sim->p_img); 
	if (result)
		return result;
	right_panel->panel->set_closable(false);

	result = heightmap_panel->init(this, ctx, phi_tex); 
	if(result)
		return result;
	heightmap_panel->panel->set_closable(false);

	result = boundary_editor->init(ctx, sim->solid_mask_img); 
	if(result)
		return result;
	boundary_editor->panel->set_closable(false);

	sim->reset(ctx);

	particles = ev2::load_graphics_pipeline(ctx, "pipelines/pde/fluid_particles.yaml");
	particle_bindings = ev2::create_bindings(ctx, particles, EV2_GFX_SET_PER_DRAW, ev2::BINDING_MODE_DYNAMIC);

	return result;
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
		sim->reset(ctx);
	}

	//ImGui::SliderFloat("gravity", &sim->uniforms.gravity, -1, 1);

	ImGui::End();

	int skip = std::max((int)(1/m_rate), 1);

	if (m_step % skip == 0 && current_step != m_step) {
		if ((result = sim->update(ctx)))
			return result;

		for (int i = 0; i < 1; ++i) {
			sim->step_sim(ctx, params);
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
		glm::vec2 pos = main_panel->get_world_cursor_pos();
		params.cursor1 = params.cursor_flags ? params.cursor2 : pos;
		params.cursor2 = pos;
		params.cursor_flags = true; 
	} else {
		params.cursor_flags = false; 
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
		
		for (int i = 0; i < GridFluidSim::DIMS; ++i) {
			ev2::cmd_use_image(pass, sim->v_pre_proj_img[i], ev2::USAGE_SAMPLED_GRAPHICS);
		}

		struct alignas(8) {
			glm::ivec2 size;
			uint32_t v_img[2];
		} pc = {
			.size = glm::ivec2(sim->grid_w, sim->grid_h),
			.v_img = {
				ev2::get_bindless_handle(ctx, v_tex[0]),
				ev2::get_bindless_handle(ctx, v_tex[1]),
			}
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
	ev2::bind_buffer(ctx, particle_bindings, "Particles", 
		sim->part_data, 0, sim->particle_count * sizeof(FluidParticle));

	ev2::flush_bindings(ctx, particle_bindings);

	struct {
		glm::mat3x2 world;
	} pc {
		.world = glm::mat3x2(
			glm::vec2(2.f/(float)sim->grid_w, 0),
			glm::vec2(0, 2.f/(float)sim->grid_h),
			glm::vec2(0, 0)
		)
	};

	ev2::cmd_use_buffer(pass, sim->part_data, ev2::USAGE_STORAGE_READ_GRAPHICS);
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
