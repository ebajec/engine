#include "app.h"
#include "utils.h"
#include "panel.h"
#include "texture_viewer.h"
#include "heightmap_viewer.h"

#include "poisson_solver.h"
#include "boundary_editor.h"
#include "grid_sim.h"
#include "gpu_sort.h"

#include <ev2/utils/log.h>
#include <ev2/utils/common.h>

#include <ev2/context.h>
#include <ev2/pipeline.h>
#include <ev2/resource.h>

#include <ev2/utils/camera_math.h>
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
	bool b_compute_bvh;
};

struct BVHHeader
{
	uint32_t elem_count;
	uint32_t internal_size;
};

struct GpuAABB2f
{
	glm::vec2 min;
	glm::vec2 max;
};

static_assert(sizeof(FluidParticle) == 16);
static_assert(offsetof(FluidParticle, pos) == 0);
static_assert(offsetof(FluidParticle, vel) == 8);

static_assert(sizeof(GPUSort::PushConstantHeader) == 32);
static_assert(offsetof(GPUSort::PushConstantHeader, in_data) == 16);

uint32_t bvh_size(uint32_t elem_count, uint32_t factor)
{
	uint32_t total = 0;
	do {
		elem_count = (elem_count + factor - 1)/factor;
		total += elem_count;
	} while (elem_count > 1);

	return total;
}

struct FLIPFluidSim
{
	uint32_t grid_w;
	uint32_t grid_h;

	static constexpr uint32_t BVH_INTERNAL_SIZE = 16;
	static constexpr uint32_t BVH_GROUP_SIZE = 128;

	static constexpr uint32_t MORTON_CODE_BITS = 24;
	static constexpr uint32_t DIMS = 2;

	struct Config {
		int num_v_cycles = 12;
	} config;

	ev2::ImageID v_pre_proj_img[DIMS];
	ev2::ImageID v_proj_img[DIMS];
	ev2::BufferID deposit_buf;

	ev2::ImageID lap_p_img; // rhs of lap(phi) = f 
	ev2::ImageID p_img; // pressure
	
	ev2::TextureID solid_tex;
	
	// solid mask: 
	// 0 = solid, 
	// 1 = free space
	ev2::ImageID solid_mask_img;
	ev2::ImageID fill_mask_img; // cell fill %

	ev2::BufferID particles;
	ev2::BufferID particles_mirror;

	ev2::BufferID particle_bvh;

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
	std::unique_ptr<GPUSort> sorter;

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

		particle_count = grid_w*grid_h;

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

		particles = ev2::create_buffer(ctx, particle_count * (sizeof(FluidParticle)),
			ev2::BUFFER_USAGE_STORAGE_BUFFER_BIT | ev2::BUFFER_USAGE_VERTEX_BUFFER_BIT); 
		particles_mirror = ev2::create_buffer(ctx, particle_count * (sizeof(FluidParticle)),
			ev2::BUFFER_USAGE_STORAGE_BUFFER_BIT | ev2::BUFFER_USAGE_VERTEX_BUFFER_BIT); 

		size_t particle_bvh_size = align_up(sizeof(BVHHeader) + sizeof(GpuAABB2f) * bvh_size(particle_count, BVH_INTERNAL_SIZE), 16);
		particle_bvh = ev2::create_buffer(ctx, particle_bvh_size, ev2::BUFFER_USAGE_STORAGE_BUFFER_BIT, 16); 

		lap_p_img = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_32F, usage);
		ev2::set_image_name(ctx, lap_p_img, "lap_p_img");

		p_img = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_32F, usage);
		ev2::set_image_name(ctx, p_img, "p_img");

		fill_mask_img = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_R8_SNORM, usage);
		ev2::set_image_name(ctx, fill_mask_img, "fill_mask");

		solid_mask_img = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_R8_UNORM, usage);
		ev2::set_image_name(ctx, solid_mask_img, "solid_mask");

		pressure_solver.reset(new PoissonSolver);

		int result = EXIT_SUCCESS;

		if ((result = pressure_solver->init(ctx, w, h)) < 0) {
			return result;
		}

		sorter.reset(GPUSort::create(ctx, particle_count, MORTON_CODE_BITS, "fluid://shader/sort_particles.slang"));

		if (!sorter) {
			return EXIT_FAILURE;
		}

		p_advect = ev2::load_compute_pipeline(ctx, "fluid://shader/nvs2_flip_advect.comp");
		p_deposit = ev2::load_compute_pipeline(ctx, "fluid://shader/nvs2_flip_deposit.comp");
		p_divergence = ev2::load_compute_pipeline(ctx, "fluid://shader/nvs2_flip_divergence.comp");
		p_project = ev2::load_compute_pipeline(ctx, "fluid://shader/nvs2_flip_project.comp");

		bindings = ev2::create_bindings(ctx, p_advect, 0, ev2::BINDING_MODE_STATIC);


		solid_tex = ev2::create_texture(ctx, solid_mask_img, ev2::FILTER_BILINEAR);

		ev2::bind_texture(ctx, bindings, "solid_mask_tex", solid_tex);
		ev2::bind_image(ctx, bindings, "solid_mask", solid_mask_img);
		ev2::bind_image(ctx, bindings, "fill_mask", fill_mask_img);

		for (int i = 0; i < DIMS; ++i) {
			ev2::bind_image_indexed(ctx, bindings, "v_pre_proj", i, v_pre_proj_img[i]);
			ev2::bind_image_indexed(ctx, bindings, "v_proj", i, v_proj_img[i]);
		}

		ev2::bind_buffer(ctx, bindings, "Particles", particles, 
			0, particle_count * sizeof(FluidParticle)); 
		ev2::bind_buffer(ctx, bindings, "VelocityBuffer", deposit_buf, 0, deposit_buf_size);  

		ev2::bind_image(ctx, bindings, "f_out", lap_p_img);
		ev2::bind_image(ctx, bindings, "p_in", p_img);

		ev2::flush_bindings(ctx, bindings);

		return 0;
	}

	int update(ev2::GfxContext *ctx)
	{
		pressure_solver->set_inputs(ctx, p_img, lap_p_img, solid_mask_img, fill_mask_img);
		return 0;
	}

	void step_sim(ev2::GfxContext *ctx, const SimParams &params)
	{
		uint32_t group_size = 16;

		uint32_t gx = 1 + grid_w/group_size;
		uint32_t gy = 1 + grid_h/group_size;

		ev2::PassID pass = ev2::begin_compute_pass(ctx, "fluid_sim_step");

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
		ev2::cmd_use_buffer(pass, particles, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);

		ev2::cmd_bind_compute_pipeline(pass, p_advect);
		ev2::cmd_dispatch(pass, 1 + (particle_count - 1)/32, 1, 1);

		//------------------------------------------------------------------------------
		// scatter/deposit stage

		ev2::cmd_use_buffer(pass, particles, ev2::USAGE_STORAGE_READ_COMPUTE);
		ev2::cmd_use_buffer(pass, deposit_buf, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
		ev2::cmd_use_image(pass, solid_mask_img, ev2::USAGE_STORAGE_READ_COMPUTE);

		ev2::cmd_bind_compute_pipeline(pass, p_deposit);
		ev2::cmd_dispatch(pass, 1 + (particle_count - 1)/128, 1, 1);

		//------------------------------------------------------------------------------
		// divergence stage

		ev2::cmd_use_buffer(pass, deposit_buf, ev2::USAGE_STORAGE_READ_COMPUTE);
		ev2::cmd_use_image(pass, lap_p_img, ev2::USAGE_STORAGE_WRITE_COMPUTE);
		ev2::cmd_use_image(pass, fill_mask_img, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);

		ev2::cmd_bind_compute_pipeline(pass, p_divergence);
		ev2::cmd_dispatch(pass, gx, gy, 1);


		if (params.b_compute_bvh) {
			//------------------------------------------------------------------------------
			// sort particles

			ev2::BufferID sort_input[] = {
				particles,
				particles_mirror
			};

			constexpr uint32_t MORTON_CODE_MAX_DIM = (1 << (MORTON_CODE_BITS + DIMS - 1)/DIMS);

			struct {
				glm::vec2 scale;
			} sort_pc = {
				.scale = glm::vec2(
					(MORTON_CODE_MAX_DIM + grid_w - 1)/grid_w,
					(MORTON_CODE_MAX_DIM + grid_h - 1)/grid_h
				)
			};

			assert(sort_input[0] != sort_input[1]);

			sorter->record(pass, MORTON_CODE_BITS, sort_input, particle_count, sizeof(FluidParticle), &sort_pc, sizeof(sort_pc));

			//------------------------------------------------------------------------------
			// compute bvh

			ev2::ComputePipelineID p_bvh_level = ev2::load_compute_pipeline(ctx, "fluid://shader/linear_bvh_2d_fluid.slang", "compute_level");

			struct {
				uint32_t in_start;
				uint32_t in_count;
				uint32_t elem_count;
				VkDeviceAddress bvh;
				VkDeviceAddress data;
			} bvh_pc = {
				.in_start = 0,
				.in_count = particle_count,
				.elem_count = particle_count,
				.bvh = ev2::get_buffer_device_address(ctx, particle_bvh),
				.data = ev2::get_buffer_device_address(ctx, particles)
			};

			ev2::cmd_bind_compute_pipeline(pass, p_bvh_level);

			// First pass, compute from data
			ev2::cmd_push_constant(pass, p_bvh_level, 0, sizeof(bvh_pc), &bvh_pc);
			ev2::cmd_use_buffer(pass, particles, ev2::USAGE_STORAGE_READ_COMPUTE);
			ev2::cmd_use_buffer(pass, particle_bvh, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
			ev2::cmd_dispatch(pass, (bvh_pc.in_count + BVH_GROUP_SIZE - 1)/BVH_GROUP_SIZE, 1, 1);

			do {
				bvh_pc.in_count = (bvh_pc.in_count + BVH_INTERNAL_SIZE - 1) / BVH_INTERNAL_SIZE;

				ev2::cmd_push_constant(pass, p_bvh_level, 0, offsetof(decltype(bvh_pc), elem_count), &bvh_pc);
				ev2::cmd_use_buffer(pass, particle_bvh, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
				ev2::cmd_dispatch(pass, (bvh_pc.in_count + BVH_GROUP_SIZE - 1)/BVH_GROUP_SIZE, 1, 1);

				bvh_pc.in_start += bvh_pc.in_count;
			} while (bvh_pc.in_count > 1);
		}
		//------------------------------------------------------------------------------
		// pressure_solve

		pressure_solver->record_setup(pass);
		for (int i = 0; i < ((step == 0) ? 4*config.num_v_cycles : config.num_v_cycles); ++i) 
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

		initialize_image<uint8_t>(ctx, fill_mask_img, UINT8_MAX); // -1
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
		ev2::destroy_image(ctx, fill_mask_img);

		ev2::destroy_buffer(ctx, particles);
		ev2::destroy_bindings(ctx, bindings);

		pressure_solver->destroy(ctx);

		sorter.reset();
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
	bool b_enable_bvh_computation = false;

	bool m_stopped = true;
	uint64_t m_step = 0;
	float m_rate = 1.f;
	int bvh_level = 0;
	int steps_per = 2;

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
	int result = app_initialize(this, argc, argv);
	if (result)
		return result;

	sim.reset(new FLIPFluidSim);

	main_panel.reset(new ImageViewerPanel(this, 200, 0, 500, 500,
		"core://pipeline/screen_quad.yaml", "Interactive Simulation"));
	right_panel.reset(new ImageViewerPanel(this, 700, 0, 500, 500, 
		"fluid://pipeline/residuals.yaml", "Residuals"));

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

	result = right_panel->init(ctx, sim->pressure_solver->R1); 
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

	particles = ev2::load_graphics_pipeline(ctx, "fluid://pipeline/fluid_particles.yaml");
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

	if (ImGui::RadioButton("Enable BVH", params.b_compute_bvh)) {
		params.b_compute_bvh = !params.b_compute_bvh;
	}

	if (ImGui::Button("Step")) {
		++m_step;
	} else if (!m_stopped) {
		++m_step;
	}

	ImGui::SliderFloat("Sim update rate", &m_rate, 1.f/256.f, 1.f);
	ImGui::SliderInt("V-cycles", &sim->config.num_v_cycles, 1, 64);
	ImGui::SliderInt("Steps/Frame", &steps_per, 1, 16);

	if (ImGui::Button("Reset")) {
		sim->reset(ctx);
	}

	//ImGui::SliderFloat("gravity", &sim->uniforms.gravity, -1, 1);

	ImGui::End();

	int skip = std::max((int)(1/m_rate), 1);

	if (m_step % skip == 0 && current_step != m_step) {
		if ((result = sim->update(ctx)))
			return result;

		for (int i = 0; i < steps_per; ++i) {
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
		glm::vec2 pos = main_panel->get_grid_cursor_pos();
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
	float aspect = (float)sim->grid_w/(float)sim->grid_h;
	glm::mat3x2 world = glm::mat3x2(
		glm::vec2(2.f*aspect/(float)sim->grid_w, 0),
		glm::vec2(0, 2.f/(float)sim->grid_h),
		glm::vec2(-aspect, -1.f)
	);

	// second pass renders with alpha blending
	ev2::GfxPassInfo pass_info = {
		.target = main_panel->panel->get_target(),
		.view = main_panel->rd.camera,
		.clear_color = true,
		.name = main_panel->panel->get_name()
	};
	ev2::PassID pass = ev2::begin_gfx_pass(ctx, &pass_info);

	struct {
		glm::vec2 size;
		VkDeviceAddress bvh;
		VkDeviceAddress parts;
	} panel_pc = {
		.size = glm::vec2(sim->grid_w , sim->grid_h),
		.bvh = ev2::get_buffer_device_address(ctx, sim->particle_bvh),
		.parts = ev2::get_buffer_device_address(ctx, sim->particles),
	};

	ev2::cmd_push_constant(pass, main_panel->rd.pipeline, 0, sizeof(panel_pc), &panel_pc);
	main_panel->record_draw(pass);

	if (b_enable_flux_arrows) {
		ev2::GfxPipelineID flux_arrows = ev2::load_graphics_pipeline(ctx, "fluid://pipeline/flux.yaml");
		
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
		sim->particles, 0, sim->particle_count * sizeof(FluidParticle));
	ev2::flush_bindings(ctx, particle_bindings);

	ev2::cmd_use_buffer(pass, sim->particles, ev2::USAGE_STORAGE_READ_GRAPHICS);
	ev2::cmd_bind_gfx_pipeline(pass, particles);
	ev2::cmd_bind_resources(pass, particle_bindings);
	ev2::cmd_push_constant(pass, particles, 0, sizeof(world), &world);
	ev2::cmd_custom(pass, [this](VkCommandBuffer cmds){
		vkCmdDraw(cmds, 6, sim->particle_count, 0, 0);
	});

	ImGui::Begin("Editor");
	ImGui::SliderInt("BVH level", &bvh_level, 0, int(floor(log(sim->particle_count)/log(FLIPFluidSim::BVH_INTERNAL_SIZE)))); 
	ImGui::End();

	if (params.b_compute_bvh) {
		uint32_t bvh_offset = 0;
		uint32_t bvh_count = sim->particle_count;
		for (int i = 0; i < bvh_level; ++i) {
			bvh_count = (bvh_count + FLIPFluidSim::BVH_INTERNAL_SIZE - 1)/FLIPFluidSim::BVH_INTERNAL_SIZE;
			bvh_offset += bvh_count;
		}

		struct {
			glm::mat3x2 world;
			uint32_t level;
			uint32_t offset;
			VkDeviceAddress bvh;
		} box_pc {
			.world = world,
			.level = (uint32_t)bvh_level,
			.offset = bvh_offset,
			.bvh = ev2::get_buffer_device_address(ctx, sim->particle_bvh)
		};

		ev2::GfxPipelineID p_boxes = ev2::load_graphics_pipeline(ctx, "fluid://pipeline/particle_box.yaml");
		ev2::cmd_use_buffer(pass, sim->particle_bvh, ev2::USAGE_STORAGE_READ_GRAPHICS);
		ev2::cmd_push_constant(pass, p_boxes, 0, sizeof(box_pc), &box_pc);
		ev2::cmd_bind_gfx_pipeline(pass, p_boxes);
		ev2::cmd_custom(pass, [bvh_count](VkCommandBuffer cmds) {
			vkCmdDraw(cmds, 5, (bvh_count + FLIPFluidSim::BVH_INTERNAL_SIZE - 1)/FLIPFluidSim::BVH_INTERNAL_SIZE, 0, 0);
		});
	}
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

__attribute__((noinline)) int frame(FluidApp * app)
{
	int status = app->begin_frame();
	if (should_exit(status))
		return status;

	status = app->update();
	if (should_exit(status))
		return status;
	
	app->render();

	status = app->end_frame();
	if (should_exit(status))
		return status;
	return status;
}

int main(int argc, char *argv[])
{
	std::unique_ptr<FluidApp> app (new FluidApp{});

	if (app->initialize(argc, argv) != App::OK)
		return EXIT_FAILURE;

	for(int status = App::OK; !should_exit(status); status = frame(app.get())) {}

	app->destroy();

	return EXIT_SUCCESS;
}
