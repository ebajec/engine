#include "app.h"
#include "panel.h"
#include "texture_viewer.h"
#include "heightmap_viewer.h"

#include <ev2/utils/log.h>

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

struct PoissonSolver
{
	ev2::ImageID tmp_lhs; // intermediate image

	ev2::ImageID R1; // level 0 upto N
	ev2::ImageID R2; // level 1 upto N + 1

	ev2::ComputePipelineID multigrid_down;
	ev2::ComputePipelineID multigrid_up;

	ev2::BindingsID bindings0;
	ev2::BindingsID bindings1;

	uint32_t sim_w = 0, sim_h = 0;

	uint32_t N = 0;

	struct Uniforms {
		uint32_t N;
		uint32_t level;
		uint32_t iterations;
	};

	int init(ev2::GfxContext *ctx, uint32_t w, uint32_t h);
	void destroy(ev2::GfxContext *ctx);

	void setup_bindings(ev2::GfxContext *ctx, ev2::ImageID phi, ev2::ImageID f);
	void record_bind(ev2::PassID pass);
	void record_v_cycle(ev2::PassID pass, ev2::GfxContext *ctx, ev2::ImageID lhs, ev2::ImageID rhs);
};

static inline constexpr bool is_pow2(size_t x)
{
	return !x || (((x - 1) & x) == 0);
}

void PoissonSolver::setup_bindings(ev2::GfxContext *ctx, ev2::ImageID lhs, ev2::ImageID rhs)
{
	ev2::reset_bindings(ctx, bindings1);
	ev2::bind_image(ctx, bindings1, "in_lhs", lhs); 
	ev2::bind_image(ctx, bindings1, "in_rhs", rhs); 
	ev2::bind_image(ctx, bindings1, "out_lhs", lhs); 
	ev2::flush_bindings(ctx, bindings1);
}

constexpr uint32_t MAX_PRESSURE_SOLVER_MIPS = 8;

int PoissonSolver::init(ev2::GfxContext *ctx, uint32_t w, uint32_t h)
{
	sim_w = w; 
	sim_h = h;

	if (!is_pow2(sim_w) || !is_pow2(sim_h))
		return EXIT_FAILURE;

	N = std::min((uint32_t)ceil(log2((double)w)), MAX_PRESSURE_SOLVER_MIPS);

	ev2::ImageUsageFlags usage = 
		ev2::IMAGE_USAGE_STORAGE_BIT | 
		ev2::IMAGE_USAGE_SAMPLED_BIT;

	R1 = ev2::create_image(ctx, sim_w, sim_h, 1, ev2::IMAGE_FORMAT_32F, usage, N); 
	ev2::set_image_name(ctx, R1, "R1");
	R2 = ev2::create_image(ctx, sim_w/2, sim_h/2, 1, ev2::IMAGE_FORMAT_32F, usage, N); 
	ev2::set_image_name(ctx, R2, "R2");

	tmp_lhs = ev2::create_image(ctx, sim_w, sim_h, 1, ev2::IMAGE_FORMAT_32F, usage);
	ev2::set_image_name(ctx, tmp_lhs, "tmp_lhs");

	multigrid_down = ev2::load_compute_pipeline(ctx, "shader/multigrid_down");
	multigrid_up = ev2::load_compute_pipeline(ctx, "shader/multigrid_up");

	//-----------------------------------------------------------------------------
	// setup bindings

	bindings0 = ev2::create_bindings(ctx, multigrid_down, 0, ev2::BINDING_MODE_STATIC);
	//ev2::bind_buffer(ctx, bindings0, "ubo", ubo, 0, sizeof(uniforms)); 
	ev2::bind_image(ctx, bindings0, "tmp_lhs", tmp_lhs); 

	for (uint32_t i = 0; i < N; ++i) {
		ev2::bind_image_indexed(ctx, bindings0, "R1", i, R1, i);
		ev2::bind_image_indexed(ctx, bindings0, "R2", i, R2, i);
	}
	ev2::flush_bindings(ctx, bindings0);

	bindings1 = ev2::create_bindings(ctx, multigrid_down, 1, ev2::BINDING_MODE_DYNAMIC);

	return EXIT_SUCCESS;
}

void PoissonSolver::destroy(ev2::GfxContext *ctx)
{
	ev2::destroy_image(ctx, R1);
	ev2::destroy_image(ctx, R2);
}

void PoissonSolver::record_bind(ev2::PassID pass)
{
	ev2::cmd_bind_resources(pass, bindings0);
	ev2::cmd_bind_resources(pass, bindings1);
}

void PoissonSolver::record_v_cycle(ev2::PassID pass, ev2::GfxContext *ctx, ev2::ImageID lhs, ev2::ImageID rhs)
{
	const uint32_t groups = 16;

	//-------------------------------------------------------------------
	// 'down' stage

	ev2::cmd_bind_compute_pipeline(pass, multigrid_down);

	ev2::cmd_use_image(pass, lhs, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
	ev2::cmd_use_image(pass, rhs, ev2::USAGE_STORAGE_READ_COMPUTE);

	ev2::cmd_use_image(pass, tmp_lhs, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);

	// Downwards pass has 0 upto N passes inclusive: One pass of jacobi iterations
	// on original, then N smooth + downsample passes on the residuals

	constexpr uint its[] = {
		4, 3, 3, 3, 2, 2, 1, 1
	};

	uint32_t tw = sim_w;
	uint32_t th = sim_h;
	for (uint32_t i = 0; i <= N; ++i) {
		uint32_t idx = i;
		Uniforms pc{
			.N = N,
			.level = idx,
			.iterations = its[idx]
		};

		uint32_t output_w = groups - 2*pc.iterations;

		ev2::cmd_use_image(pass, R1, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
		ev2::cmd_use_image(pass, R2, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);

		ev2::cmd_push_constant(pass, multigrid_down, 0, sizeof(pc), &pc);
		ev2::cmd_dispatch(pass, 1 + (tw - 1)/output_w, 1 + (th - 1)/output_w, 1);

		tw = 1 + (tw - 1)/2;
		th = 1 + (th - 1)/2;
	}

	//-------------------------------------------------------------------
	// 'up' stage
	
	ev2::cmd_bind_compute_pipeline(pass, multigrid_up);

	ev2::cmd_use_image(pass, tmp_lhs, ev2::USAGE_STORAGE_READ_COMPUTE);

	tw *= 2;
	th *= 2;

	// N-1, N-2, ... 0
	for (uint32_t i = 0; i < N; ++i) {
		uint32_t idx = (N - 1 - i); 
		Uniforms pc{
			.N = N,
			.level = idx,
			.iterations = its[idx]
		};
		uint32_t output_w = groups - 2*pc.iterations;

		ev2::cmd_use_image(pass, R1, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
		ev2::cmd_use_image(pass, R2, ev2::USAGE_STORAGE_READ_COMPUTE);

		ev2::cmd_push_constant(pass, multigrid_down, 0, sizeof(pc), &pc);

		tw *= 2;
		th *= 2;
		ev2::cmd_dispatch(pass, 1 + (tw - 1)/output_w, 1 + (th - 1)/output_w, 1);
	}
}

struct MeanSubtractor
{
	ev2::ComputePipelineID accumulate16;
	ev2::ComputePipelineID subtract_img;

	ev2::BindingsID accumulate_set[4];
	ev2::BindingsID subtract_set;

	ev2::ImageID downsamples[4] = {};
	ev2::TextureID final_layer;
	ev2::ImageID out_img;

	ev2::BindingSlot accumulate_input_slot;
	ev2::BindingSlot subtract_output_slot;
	
	uint32_t levels;
	uint32_t width, height;
	
	int init(ev2::GfxContext *ctx, uint32_t w, uint32_t h);
	int destroy(ev2::GfxContext *ctx);
	void record(ev2::PassID pass);

	void setup_bindings(ev2::GfxContext *ctx, ev2::ImageID img);
};

int MeanSubtractor::init(ev2::GfxContext *ctx, uint32_t w, uint32_t h)
{
	accumulate16 = ev2::load_compute_pipeline(ctx, "shader/accumulate16");
	subtract_img = ev2::load_compute_pipeline(ctx, "shader/subtract_img");

	width = w;
	height = h;
	levels = (uint32_t)(ceil(std::max(log((double)w),log((double)h))/log(16.)));

	ev2::ImageUsageFlags usage = ev2::IMAGE_USAGE_STORAGE_BIT |
	                   ev2::IMAGE_USAGE_SAMPLED_BIT;

	for (uint32_t i = 0; i < levels; ++i) {
		w = 1 + (w - 1)/16;
		h = 1 + (h - 1)/16;
		downsamples[i] = ev2::create_image(ctx, w, h, 1, ev2::IMAGE_FORMAT_32F, usage);
	}

	assert(w == 1 && h == 1);

	final_layer = ev2::create_texture(ctx, downsamples[levels - 1], ev2::FILTER_NEAREST);

	accumulate_set[0] = ev2::create_bindings(ctx, accumulate16, 0, 
									   ev2::BINDING_MODE_DYNAMIC);

	for (uint32_t i = 1; i < levels; ++i) {
		accumulate_set[i] = ev2::create_bindings(ctx, accumulate16, 0,
										ev2::BINDING_MODE_STATIC);
		ev2::bind_image(ctx, accumulate_set[i], "img_in", downsamples[i-1]);
		ev2::bind_image(ctx, accumulate_set[i], "img_out", downsamples[i]);
	}
	for (uint32_t i = 1; i < levels; ++i)
		ev2::flush_bindings(ctx, accumulate_set[i]);

	subtract_set = ev2::create_bindings(ctx, subtract_img, 0, ev2::BINDING_MODE_DYNAMIC);

	return 0;
}
int MeanSubtractor::destroy(ev2::GfxContext *ctx)
{
	for (uint32_t i = 0; i < levels; ++i) {
		ev2::destroy_image(ctx, downsamples[i]);
		ev2::destroy_bindings(ctx, accumulate_set[i]);
	}
	ev2::destroy_texture(ctx, final_layer);
	ev2::destroy_bindings(ctx, subtract_set);

	return 0;
}

void MeanSubtractor::setup_bindings(ev2::GfxContext *ctx, ev2::ImageID img)
{
	ev2::reset_bindings(ctx, accumulate_set[0]);
	ev2::bind_image(ctx, accumulate_set[0], "img_in", img);
	ev2::bind_image(ctx, accumulate_set[0], "img_out", downsamples[0]);
	ev2::flush_bindings(ctx, accumulate_set[0]);

	ev2::reset_bindings(ctx, subtract_set);
	ev2::bind_texture(ctx, subtract_set, "img_in", final_layer);
	ev2::bind_image(ctx, subtract_set, "img_out", img);
	ev2::flush_bindings(ctx, subtract_set);

	out_img = img;
}

void MeanSubtractor::record(ev2::PassID pass)
{
	ev2::cmd_bind_compute_pipeline(pass, accumulate16);

	uint32_t w = width;
	uint32_t h = height;

	for (uint32_t i = 0; i < levels; ++i) {
		ev2::cmd_use_image(pass, downsamples[i], ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
		if (i > 0)
			ev2::cmd_use_image(pass, downsamples[i - 1], ev2::USAGE_STORAGE_READ_COMPUTE);

		ev2::cmd_bind_resources(pass, accumulate_set[i]);

		uint32_t gx = 1 + (w- 1)/16;
		uint32_t gy = 1 + (h - 1)/16;

		ev2::cmd_dispatch(pass, gx, gy, 1);

		w = 1 + (w-1)/16;
		h = 1 + (h-1)/16;
	}

	ev2::cmd_use_image(pass, out_img, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE); 
	ev2::cmd_bind_compute_pipeline(pass, subtract_img);
	ev2::cmd_bind_resources(pass, subtract_set);
	ev2::cmd_dispatch(pass, 1 + (width-1)/16, 1 + (height-1)/16, 1);
}

struct FluidSim
{
	uint32_t grid_w;
	uint32_t grid_h;

	ev2::ImageID q_img_1; // pre-advection state
	ev2::ImageID q_img_2; // post-advection state

	ev2::ImageID lap_p_img; // rhs of lap(phi) = f 
	ev2::ImageID p_img; // pressure
	
	ev2::TextureID q_tex_1; // pre-advection state
	ev2::TextureID q_tex_2; // post-advection state

	ev2::TextureID lap_p_tex; // rhs of lap(phi) = f 
	ev2::TextureID p_tex; // pressure
	
	ev2::ImageID mask_img;
	ev2::TextureID mask_tex;
	
	ev2::BufferID ubo;

	ev2::ComputePipelineID nvs_advect;
	ev2::ComputePipelineID nvs_diffuse;
	ev2::ComputePipelineID nvs_pressure;
	ev2::ComputePipelineID nvs_project;

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
	int update_pressure_set(ev2::GfxContext *ctx);
	int update_project_set(ev2::GfxContext *ctx);

	int init(ev2::GfxContext *ctx, uint32_t w, uint32_t h);
	int update(ev2::GfxContext *ctx);
	void destroy(ev2::GfxContext *ctx);
};

int FluidSim::init(ev2::GfxContext *ctx, uint32_t w, uint32_t h)
{
	if (!is_pow2(w) || !is_pow2(h))
		return EXIT_FAILURE;

	grid_w = w;
	grid_h = h;

	ev2::ImageUsageFlags usage = 
		ev2::IMAGE_USAGE_STORAGE_BIT | 
		ev2::IMAGE_USAGE_SAMPLED_BIT;

	q_img_1 = ev2::create_image(ctx, 1 + grid_w, 1 + grid_h, 1, ev2::IMAGE_FORMAT_RGBA32F, usage);
	q_img_2 = ev2::create_image(ctx, 1 + grid_w, 1 + grid_h, 1, ev2::IMAGE_FORMAT_RGBA32F, usage);

	lap_p_img = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_32F, usage);
	ev2::set_image_name(ctx, lap_p_img, "lap_p");

	p_img = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_32F, usage);
	ev2::set_image_name(ctx, p_img, "p");

	mask_img = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_R8_UNORM, usage);

	q_tex_1 = ev2::create_texture(ctx, q_img_1, ev2::FILTER_BILINEAR);
	q_tex_2 = ev2::create_texture(ctx, q_img_2, ev2::FILTER_BILINEAR);

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

	nvs_advect = ev2::load_compute_pipeline(ctx, "shader/nvs_advect");
	nvs_diffuse = ev2::load_compute_pipeline(ctx, "shader/nvs_diffuse");
	nvs_pressure = ev2::load_compute_pipeline(ctx, "shader/nvs_pressure");
	nvs_project = ev2::load_compute_pipeline(ctx, "shader/nvs_project");

	update_advect_set(ctx);
	update_diffuse_set(ctx);
	update_pressure_set(ctx);
	update_project_set(ctx);

	return 0;
}

int FluidSim::update_advect_set(ev2::GfxContext *ctx)
{
	ev2::BindingsID set = ev2::create_bindings(ctx, nvs_advect, 0, ev2::BINDING_MODE_STATIC);
	ev2::bind_texture(ctx, set, "q_in", q_tex_1);
	ev2::bind_image(ctx, set, "q_out", q_img_2);
	ev2::bind_buffer(ctx, set, "ubo", ubo, 0, sizeof(Uniforms));
	ev2::flush_bindings(ctx, set);

	advect_set = set;

	return 0;
}

int FluidSim::update_diffuse_set(ev2::GfxContext *ctx)
{
	ev2::BindingsID set = ev2::create_bindings(ctx, nvs_diffuse, 0, ev2::BINDING_MODE_STATIC);
	ev2::bind_image(ctx, set, "q_in", q_img_2);
	ev2::bind_image(ctx, set, "q_out", q_img_1);
	ev2::bind_buffer(ctx, set, "ubo", ubo, 0, sizeof(Uniforms));
	ev2::flush_bindings(ctx, set);

	diffuse_set = set;

	return 0;
}

int FluidSim::update_pressure_set(ev2::GfxContext *ctx)
{
	ev2::BindingsID set = ev2::create_bindings(ctx, nvs_pressure, 0, ev2::BINDING_MODE_STATIC);
	ev2::bind_texture(ctx, set, "q_in", q_tex_1);
	ev2::bind_image(ctx, set, "f_out", lap_p_img);
	ev2::bind_buffer(ctx, set, "ubo", ubo, 0, sizeof(Uniforms));
	ev2::flush_bindings(ctx, set);

	pressure_set = set;

	return 0;
}
int FluidSim::update_project_set(ev2::GfxContext *ctx)
{
	ev2::BindingsID set = ev2::create_bindings(ctx, nvs_project, 0, ev2::BINDING_MODE_STATIC);
	ev2::bind_image(ctx, set, "q_img", q_img_1);
	ev2::bind_texture(ctx, set, "p_in", p_tex);
	ev2::bind_texture(ctx, set, "lap_p_in", lap_p_tex);
	ev2::bind_buffer(ctx, set, "ubo", ubo, 0, sizeof(Uniforms));
	ev2::flush_bindings(ctx, set);

	project_set = set;

	return 0;
}

int FluidSim::update(ev2::GfxContext *ctx)
{
	ev2::UploadContext uc = ev2::begin_upload(ctx, sizeof(Uniforms), alignof(Uniforms));
	memcpy(uc.ptr, &uniforms, sizeof(Uniforms));
	ev2::BufferUpload up = {.size = sizeof(Uniforms)};
	uint64_t sync = ev2::commit_buffer_uploads(ctx, uc, ubo, &up, 1);
	ev2::flush_uploads(ctx);

	uint32_t group_size = 16;

	uint32_t gx = 1 + grid_w/group_size;
	uint32_t gy = 1 + grid_h/group_size;

	mean_subtractor->setup_bindings(ctx, lap_p_img);
	pressure_solver->setup_bindings(ctx, p_img, lap_p_img);

	ev2::PassID pass = ev2::begin_compute_pass(ctx);

	ev2::cmd_use_buffer(pass, ubo, ev2::USAGE_UNIFORM_READ);
	ev2::cmd_use_image(pass, q_img_1, ev2::USAGE_SAMPLED_COMPUTE);
	ev2::cmd_use_image(pass, q_img_2, ev2::USAGE_STORAGE_WRITE_COMPUTE);

	ev2::cmd_bind_compute_pipeline(pass, nvs_advect);
	ev2::cmd_bind_resources(pass, advect_set);
	ev2::cmd_dispatch(pass, gx, gy, 1);

	ev2::cmd_use_image(pass, q_img_1, ev2::USAGE_STORAGE_WRITE_COMPUTE);
	ev2::cmd_use_image(pass, q_img_2, ev2::USAGE_STORAGE_READ_COMPUTE);

	ev2::cmd_bind_compute_pipeline(pass, nvs_diffuse);
	ev2::cmd_bind_resources(pass, diffuse_set);
	ev2::cmd_dispatch(pass, gx, gy, 1);

	ev2::cmd_use_image(pass, q_img_1, ev2::USAGE_SAMPLED_COMPUTE);
	ev2::cmd_use_image(pass, lap_p_img, ev2::USAGE_STORAGE_WRITE_COMPUTE);

	ev2::cmd_bind_compute_pipeline(pass, nvs_pressure);
	ev2::cmd_bind_resources(pass, pressure_set);
	ev2::cmd_dispatch(pass, gx, gy, 1);

	//mean_subtractor->record(pass);
	
	pressure_solver->record_bind(pass);
	for (int i = 0; i < ((step == 0) ? 64 : 5); ++i) 
		pressure_solver->record_v_cycle(pass, ctx, p_img, lap_p_img);

	ev2::cmd_use_image(pass, q_img_1, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
	ev2::cmd_use_image(pass, p_img, ev2::USAGE_SAMPLED_COMPUTE);
	ev2::cmd_use_image(pass, lap_p_img, ev2::USAGE_SAMPLED_COMPUTE);

	ev2::cmd_bind_compute_pipeline(pass, nvs_project);
	ev2::cmd_bind_resources(pass, project_set);
	ev2::cmd_dispatch(pass, gx, gy, 1);

	//mean_subtractor->set_image(ctx, p_img);
	//mean_subtractor->record(rec);

	ev2::end_pass(ctx, pass);

	 ++step;

	return 0;
}
void FluidSim::destroy(ev2::GfxContext *ctx)
{
	pressure_solver->destroy(ctx);
	mean_subtractor->destroy(ctx);

	ev2::destroy_image(ctx, q_img_1);
	ev2::destroy_image(ctx, q_img_2);

	ev2::destroy_image(ctx, lap_p_img);
	ev2::destroy_image(ctx, p_img);
	
	ev2::destroy_texture(ctx, q_tex_1);
	ev2::destroy_texture(ctx, q_tex_2);

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
	std::unique_ptr<HeightmapViewerPanel> heightmap_panel;

	ev2::GfxPipelineID vector_field_pipe;
	ev2::BindingsID vector_field_set;

	ev2::TextureID phi_tex;
	ev2::TextureID f_tex;

	bool b_enable_right_panel = false;
	bool b_heightmap_panel = true;
	bool b_main_panel = true;

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
				  "pipelines/fluid_viz.yaml", "Interactive Simulation")
				  );
	right_panel.reset(new ImageViewerPanel(this, 700, 0, 500, 500, 
										  "pipelines/pressure_viz.yaml", 
										"Pressure Debug"));

	heightmap_panel.reset(new HeightmapViewerPanel());

	result = sim->init(ctx, 512, 512);
	if (result)
		return result;

	phi_tex = ev2::create_texture(ctx, sim->q_img_1, ev2::FILTER_BILINEAR);
	f_tex = ev2::create_texture(ctx, sim->q_img_1, ev2::FILTER_BILINEAR);

	vector_field_pipe = ev2::load_graphics_pipeline(ctx, "pipelines/vector_field.yaml");
	{
		vector_field_set = ev2::create_bindings(ctx, vector_field_pipe, 
										  EV2_GFX_SET_PER_DRAW, ev2::BINDING_MODE_STATIC);
		ev2::bind_texture(ctx, vector_field_set, "u_tex", phi_tex);
		ev2::flush_bindings(ctx, vector_field_set);
	}

	result = main_panel->init(ctx, sim->q_img_1); 
	if (result)
		return result;
	main_panel->panel->set_closable(false);

	result = right_panel->init(ctx, sim->q_img_1); 
	if (result)
		return result;
	right_panel->panel->set_closable(false);

	result = heightmap_panel->init(this, ctx, sim->q_tex_1); 
	if(result)
		return result;
	heightmap_panel->panel->set_closable(false);

	reset_images();

	return result;
}

void FluidApp::reset_images()
{
	upload_img_data(ctx, sim->p_img, sim->grid_w, sim->grid_h);
	upload_img_data(ctx, sim->q_img_1, 1 + sim->grid_w, 1 + sim->grid_h);
	upload_img_data(ctx, sim->q_img_2, 1 + sim->grid_w, 1 + sim->grid_h);

	ev2::flush_uploads(ctx);

	sim->uniforms.cursor = sim->uniforms.cursor_prev = glm::vec2(1,0.5);
}

int FluidApp::update()
{
	int result = EXIT_SUCCESS;
	uint64_t current_step = m_step;

	ImGui::Begin("Editor");

	if (ImGui::Checkbox("Stopped", &m_stopped)) {
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
	}
	result = main_panel->update(ctx);
	if (result < App::OK)
		return result;

	result = right_panel->update(ctx);
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
	main_panel->render(ctx);

	//vector field arrows
	//ev2::PassID pass = main_panel->begin_pass(ctx);
	//ev2::cmd_bind_gfx_pipeline(pass, vector_field_pipe);
	//ev2::cmd_bind_resources(pass, vector_field_set);

	//glDisable(GL_DEPTH_TEST);
	//glEnable(GL_BLEND);
	//glBlendEquation(GL_FUNC_ADD);
	//glBlendFunc(GL_ONE, GL_ONE);	

	//size_t count = sim->grid_w * sim->grid_h;
	//const uint32_t indices[] = {0, 1};
	//glDrawElementsInstanced(GL_LINES, 2, GL_UNSIGNED_INT, indices, count);
	//glDisable(GL_BLEND);

	//ev2::end_pass(ctx, pass);

	right_panel->render(ctx);
	heightmap_panel->render(ctx);
}
void FluidApp::destroy()
{
	main_panel->destroy(ctx);
	right_panel->destroy(ctx);
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
