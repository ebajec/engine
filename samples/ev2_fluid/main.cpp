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

struct PressureSolver
{
	ev2::ImageID lhs_tmp; // intermediate image

	ev2::ImageID R1; // level 0 upto N
	ev2::ImageID R2; // level 1 upto N + 1
	
	ev2::BufferID ubo;

	ev2::ComputePipelineID multigrid_down;
	ev2::ComputePipelineID multigrid_up;

	ev2::DescriptorSetID down_set, up_set;

	uint32_t sim_w = 0, sim_h = 0;

	uint32_t N = 0;

	struct {
		ev2::BindingSlot ubo;
		ev2::BindingSlot in_lhs;
		ev2::BindingSlot in_rhs;
		ev2::BindingSlot R1;
		ev2::BindingSlot R2;
		ev2::BindingSlot tmp_lhs_id;
	} down_slots;

	struct {
		ev2::BindingSlot ubo;
		ev2::BindingSlot out_lhs;
		ev2::BindingSlot tmp_lhs_id;
		ev2::BindingSlot R1;
		ev2::BindingSlot R2;
	} up_slots;

	struct Uniforms {
		uint32_t N;
		uint32_t level;
		uint32_t iterations;
	};

	std::vector<Uniforms> uniforms;

	int init(ev2::Device *dev, uint32_t w, uint32_t h);
	void destroy(ev2::Device *dev);

	void v_cycle(ev2::RecorderID rec, ev2::Device *dev, ev2::ImageID phi, ev2::ImageID f);
};

static inline constexpr bool is_pow2(size_t x)
{
	return !x || (((x - 1) & x) == 0);
}

int PressureSolver::init(ev2::Device *dev, uint32_t w, uint32_t h)
{
	sim_w = w; 
	sim_h = h;

	if (!is_pow2(sim_w) || !is_pow2(sim_h))
		return EXIT_FAILURE;

	N = std::min((int)ceil(log2((double)w)), 6);

	R1 = ev2::create_image(dev, sim_w, sim_h, 1, ev2::IMAGE_FORMAT_32F, N); 
	R2 = ev2::create_image(dev, sim_w/2, sim_h/2, 1, ev2::IMAGE_FORMAT_32F, N); 

	lhs_tmp = ev2::create_image(dev, sim_w, sim_h, 1, ev2::IMAGE_FORMAT_32F);

	multigrid_down = ev2::load_compute_pipeline(dev, "shader/multigrid_down.comp.spv");

	ev2::DescriptorLayoutID down_layout = 
		ev2::get_compute_pipeline_layout(dev, multigrid_down);

	down_slots.ubo = ev2::find_binding(down_layout, "ubo");
	down_slots.in_lhs = ev2::find_binding(down_layout, "in_lhs");
	down_slots.in_rhs = ev2::find_binding(down_layout, "in_rhs");
	down_slots.R1 = ev2::find_binding(down_layout, "R1");
	down_slots.R2 = ev2::find_binding(down_layout, "R2");
	down_slots.tmp_lhs_id = ev2::find_binding(down_layout, "tmp_lhs");

	multigrid_up = ev2::load_compute_pipeline(dev, "shader/multigrid_up.comp.spv");

	ev2::DescriptorLayoutID up_layout = 
		ev2::get_compute_pipeline_layout(dev, multigrid_up);

	up_slots.ubo = ev2::find_binding(up_layout, "ubo");
	up_slots.out_lhs = ev2::find_binding(up_layout, "out_lhs");
	up_slots.tmp_lhs_id = ev2::find_binding(up_layout, "tmp_lhs");
	up_slots.R1 = ev2::find_binding(up_layout, "R1");
	up_slots.R2 = ev2::find_binding(up_layout, "R2");

	//--------------------------------------------------------------------
	// create ubo
	
	uniforms.resize(N + 1);
	for (uint32_t i = 0; i <= N; ++i) {
		uint32_t its = 4;
		uniforms[i] = Uniforms{
			.N = N,
			.level = i,
			.iterations = 4
		};
	}

	uniforms[0].iterations = 4;

	size_t ubo_size = (N + 1) * sizeof(Uniforms);

	ubo = ev2::create_buffer(dev, ubo_size);

	ev2::UploadContext uc = ev2::begin_upload(dev, ubo_size, alignof(Uniforms));
	memcpy(uc.ptr, uniforms.data(), uniforms.size()*sizeof(Uniforms));
	ev2::BufferUpload up = {
		.size = ubo_size
	};
	uint64_t sync = ev2::commit_buffer_uploads(dev, uc, ubo, &up, 1);

	return EXIT_SUCCESS;
}

void PressureSolver::destroy(ev2::Device *dev)
{
	ev2::destroy_image(dev, R1);
	ev2::destroy_image(dev, R2);

	ev2::destroy_buffer(dev, ubo);
}

void PressureSolver::v_cycle(ev2::RecorderID rec, ev2::Device *dev, ev2::ImageID lhs, ev2::ImageID rhs)
{
	GLuint phi_final_id = ev2::get_image_gpu_handle(dev, lhs);
	GLuint f_id = ev2::get_image_gpu_handle(dev, rhs);
	GLuint tmp_lhs_id = ev2::get_image_gpu_handle(dev, lhs_tmp);

	GLuint R1_id = ev2::get_image_gpu_handle(dev, R1);
	GLuint R2_id = ev2::get_image_gpu_handle(dev, R2);

	GLuint ubo_id = ev2::get_buffer_gpu_handle(dev, ubo);

	const uint32_t groups = 32;

	//-------------------------------------------------------------------
	// 'down' stage

	glBindImageTexture(down_slots.in_lhs.id, phi_final_id, 
		0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F
	);
	glBindImageTexture(down_slots.in_rhs.id, f_id, 
		0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F
	);
	glBindImageTexture(down_slots.tmp_lhs_id.id, tmp_lhs_id, 
		0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F
	);

	for (uint32_t i = 0; i < N; ++i) {
		glBindImageTexture(down_slots.R1.id + i, R1_id,
			i, GL_FALSE, 0, GL_READ_WRITE, GL_R32F);
		glBindImageTexture(down_slots.R2.id + i, R2_id,
			i, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
	}

	ev2::cmd_bind_compute_pipeline(rec, multigrid_down);


	// Downwards pass has 0 upto N passes inclusive: One pass of jacobi iterations
	// on original, then N smooth + downsample passes on the residuals

	uint32_t tw = sim_w;
	uint32_t th = sim_h;
	for (uint32_t i = 0; i <= N; ++i) {
		uint32_t its = uniforms[i].iterations;
		uint32_t output_w = groups - 2*its;

		glBindBufferRange(GL_UNIFORM_BUFFER, down_slots.ubo.id, ubo_id, 
					i*sizeof(Uniforms), sizeof(Uniforms));

		ev2::cmd_dispatch(rec, 1 + (tw - 1)/output_w, 1 + (th - 1)/output_w, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		tw = 1 + (tw - 1)/2;
		th = 1 + (th - 1)/2;
	}

	//-------------------------------------------------------------------
	// 'up' stage
	
	glBindImageTexture(up_slots.out_lhs.id, phi_final_id,
		0, GL_FALSE, 0, GL_READ_WRITE, GL_R32F);

	glBindImageTexture(up_slots.tmp_lhs_id.id, tmp_lhs_id,
		0, GL_FALSE, 0, GL_READ_WRITE, GL_R32F);


	for (uint32_t i = 0; i < N; ++i) {
		glBindImageTexture(up_slots.R1.id + i, R1_id,
			i, GL_FALSE, 0, GL_READ_WRITE, GL_R32F);
		glBindImageTexture(up_slots.R2.id + i, R2_id,
			i, GL_FALSE, 0, GL_READ_WRITE, GL_R32F);
	}

	ev2::cmd_bind_compute_pipeline(rec, multigrid_up);

	tw *= 2;
	th *= 2;
	// N-1, N-2, ... 0
	for (uint32_t i = 0; i < N; ++i) {
		uint32_t its = uniforms[i].iterations;
		uint32_t output_w = groups - 2*its;

		glBindBufferRange(GL_UNIFORM_BUFFER, up_slots.ubo.id, ubo_id, 
					(N - 1 - i)*sizeof(Uniforms), sizeof(Uniforms));
		tw *= 2;
		th *= 2;
		ev2::cmd_dispatch(rec, 1 + (tw - 1)/output_w, 1 + (th - 1)/output_w, 1);

		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	}
}

struct MeanSubtractor
{
	ev2::ComputePipelineID accumulate16;
	ev2::ComputePipelineID subtract_img;

	ev2::DescriptorSetID accumulate_set[4];

	ev2::DescriptorSetID subtract_set;

	ev2::ImageID downsamples[4] = {};
	ev2::TextureID final_layer;

	ev2::BindingSlot accumulate_input_slot;
	ev2::BindingSlot subtract_output_slot;
	
	uint32_t levels;
	uint32_t width, height;
	
	int init(ev2::Device *dev, uint32_t w, uint32_t h);
	int destroy(ev2::Device *dev);
	void record(ev2::RecorderID rec);

	void set_image(ev2::Device *dev, ev2::ImageID img);
};

int MeanSubtractor::init(ev2::Device *dev, uint32_t w, uint32_t h)
{
	accumulate16 = ev2::load_compute_pipeline(dev, "shader/accumulate16.comp.spv");
	subtract_img = ev2::load_compute_pipeline(dev, "shader/subtract_img.comp.spv");

	width = w;
	height = h;
	levels = (uint32_t)(ceil(std::max(log((double)w),log((double)h))/log(16.)));
	
	for (uint32_t i = 0; i < levels; ++i) {
		w = 1 + (w - 1)/16;
		h = 1 + (h - 1)/16;
		downsamples[i] = ev2::create_image(dev, w, h, 1, ev2::IMAGE_FORMAT_32F);
	}

	assert(w == 1 && h == 1);

	final_layer = ev2::create_texture(dev, downsamples[levels - 1], ev2::FILTER_NEAREST);

	{
		ev2::DescriptorLayoutID layout = ev2::get_compute_pipeline_layout(dev, accumulate16);
		ev2::BindingSlot in_slot = ev2::find_binding(layout, "img_in");
		ev2::BindingSlot out_slot = ev2::find_binding(layout, "img_out");

		accumulate_set[0] = ev2::create_descriptor_set(dev, layout);
		accumulate_input_slot = in_slot;

		ev2::bind_image(dev, accumulate_set[0], out_slot, downsamples[0]);

		for (uint32_t i = 1; i < levels; ++i) {
			accumulate_set[i] = ev2::create_descriptor_set(dev, layout);
			ev2::bind_image(dev, accumulate_set[i], in_slot, downsamples[i-1]);
			ev2::bind_image(dev, accumulate_set[i], out_slot, downsamples[i]);
		}
	}
	{
		ev2::DescriptorLayoutID layout = ev2::get_compute_pipeline_layout(dev, subtract_img);
		ev2::BindingSlot in_slot = ev2::find_binding(layout, "img_in");

		subtract_output_slot = ev2::find_binding(layout, "img_out");

		subtract_set = ev2::create_descriptor_set(dev, layout);

		ev2::bind_texture(dev, subtract_set, in_slot, final_layer);

	}

	return 0;
}
int MeanSubtractor::destroy(ev2::Device *dev)
{
	for (uint32_t i = 0; i < levels; ++i) {
		ev2::destroy_image(dev, downsamples[i]);
		ev2::destroy_descriptor_set(dev, accumulate_set[i]);
	}
	ev2::destroy_texture(dev, final_layer);
	ev2::destroy_descriptor_set(dev, subtract_set);

	return 0;
}

void MeanSubtractor::set_image(ev2::Device *dev, ev2::ImageID img)
{
	ev2::bind_image(dev, accumulate_set[0], accumulate_input_slot, img);
	ev2::bind_image(dev, subtract_set, subtract_output_slot, img);
}

void MeanSubtractor::record(ev2::RecorderID rec)
{
	ev2::cmd_bind_compute_pipeline(rec, accumulate16);

	uint32_t w = width;
	uint32_t h = height;

	for (uint32_t i = 0; i < levels; ++i) {
		ev2::cmd_bind_descriptor_set(rec, accumulate_set[i]);

		uint32_t gx = 1 + (w- 1)/16;
		uint32_t gy = 1 + (h - 1)/16;

		ev2::cmd_dispatch(rec, gx, gy, 1);

		w = 1 + (w-1)/16;
		h = 1 + (h-1)/16;

		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	}

	ev2::cmd_bind_compute_pipeline(rec, subtract_img);
	ev2::cmd_bind_descriptor_set(rec, subtract_set);
	ev2::cmd_dispatch(rec, 1 + (width-1)/16, 1 + (height-1)/16, 1);

	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
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

	ev2::DescriptorSetID advect_set;
	ev2::DescriptorSetID diffuse_set;
	ev2::DescriptorSetID pressure_set;
	ev2::DescriptorSetID project_set;

	std::unique_ptr<PressureSolver> pressure_solver;
	std::unique_ptr<MeanSubtractor> mean_subtractor;

	uint64_t step = 0;

	struct Uniforms {
		glm::vec2 cursor = glm::vec2(1,0.5);
		glm::vec2 cursor_prev;
		uint32_t flags;
		float gravity = 0;
	} uniforms;

	int update_advect_set(ev2::Device *dev);
	int update_diffuse_set(ev2::Device *dev);
	int update_pressure_set(ev2::Device *dev);
	int update_project_set(ev2::Device *dev);

	int init(ev2::Device *dev, uint32_t w, uint32_t h);
	int update(ev2::Device *dev);
	void destroy(ev2::Device *dev);
};

int FluidSim::init(ev2::Device *dev, uint32_t w, uint32_t h)
{
	if (!is_pow2(w) || !is_pow2(h))
		return EXIT_FAILURE;

	grid_w = w;
	grid_h = h;

	q_img_1 = ev2::create_image(dev, 1 + grid_w, 1 + grid_h, 1, ev2::IMAGE_FORMAT_RGBA32F);
	q_img_2 = ev2::create_image(dev, 1 + grid_w, 1 + grid_h, 1, ev2::IMAGE_FORMAT_RGBA32F);

	lap_p_img = ev2::create_image(dev, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_32F);
	p_img = ev2::create_image(dev, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_32F);

	mask_img = ev2::create_image(dev, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_R8_UNORM);

	q_tex_1 = ev2::create_texture(dev, q_img_1, ev2::FILTER_BILINEAR);
	q_tex_2 = ev2::create_texture(dev, q_img_2, ev2::FILTER_BILINEAR);

	lap_p_tex = ev2::create_texture(dev, lap_p_img, ev2::FILTER_NEAREST);
	p_tex = ev2::create_texture(dev, p_img, ev2::FILTER_NEAREST);

	ubo = ev2::create_buffer(dev, sizeof(uniforms));

	pressure_solver.reset(new PressureSolver);

	int result = EXIT_SUCCESS;

	if ((result = pressure_solver->init(dev, w, h))) {
		return result;
	}

	mean_subtractor.reset(new MeanSubtractor);

	if ((result = mean_subtractor->init(dev, grid_w, grid_h))) {
		return result;
	}

	nvs_advect = ev2::load_compute_pipeline(dev, "shader/nvs_advect.comp.spv");
	nvs_diffuse = ev2::load_compute_pipeline(dev, "shader/nvs_diffuse.comp.spv");
	nvs_pressure = ev2::load_compute_pipeline(dev, "shader/nvs_pressure.comp.spv");
	nvs_project = ev2::load_compute_pipeline(dev, "shader/nvs_project.comp.spv");

	update_advect_set(dev);
	update_diffuse_set(dev);
	update_pressure_set(dev);
	update_project_set(dev);

	return 0;
}

int FluidSim::update_advect_set(ev2::Device *dev)
{
	ev2::DescriptorLayoutID layout = ev2::get_compute_pipeline_layout(dev, nvs_advect);

	ev2::DescriptorSetID set = ev2::create_descriptor_set(dev, layout);
	ev2::bind_texture(dev, set, ev2::find_binding(layout, "q_in"), q_tex_1);
	ev2::bind_image(dev, set, ev2::find_binding(layout, "q_out"), q_img_2);
	ev2::bind_buffer(dev, set, ev2::find_binding(layout, "ubo"), ubo, 0, sizeof(Uniforms));

	advect_set = set;

	return 0;
}

int FluidSim::update_diffuse_set(ev2::Device *dev)
{
	ev2::DescriptorLayoutID layout = ev2::get_compute_pipeline_layout(dev, nvs_diffuse);

	ev2::DescriptorSetID set = ev2::create_descriptor_set(dev, layout);

	ev2::bind_image(dev, set, ev2::find_binding(layout, "q_in"), q_img_2);
	ev2::bind_image(dev, set, ev2::find_binding(layout, "q_out"), q_img_1);
	ev2::bind_buffer(dev, set, ev2::find_binding(layout, "ubo"), ubo, 0, sizeof(Uniforms));

	diffuse_set = set;

	return 0;
}

int FluidSim::update_pressure_set(ev2::Device *dev)
{
	ev2::DescriptorLayoutID layout = ev2::get_compute_pipeline_layout(dev, nvs_pressure);

	ev2::DescriptorSetID set = ev2::create_descriptor_set(dev, layout);
	ev2::bind_texture(dev, set, ev2::find_binding(layout, "q_in"), q_tex_1);
	ev2::bind_image(dev, set, ev2::find_binding(layout, "f_out"), lap_p_img);
	ev2::bind_buffer(dev, set, ev2::find_binding(layout, "ubo"), ubo, 0, sizeof(Uniforms));

	pressure_set = set;

	return 0;
}
int FluidSim::update_project_set(ev2::Device *dev)
{
	ev2::DescriptorLayoutID layout = ev2::get_compute_pipeline_layout(dev, nvs_project);

	ev2::DescriptorSetID set = ev2::create_descriptor_set(dev, layout);
	ev2::bind_image(dev, set, ev2::find_binding(layout, "q_img"), q_img_1);
	ev2::bind_texture(dev, set, ev2::find_binding(layout, "p_in"), p_tex);
	ev2::bind_texture(dev, set, ev2::find_binding(layout, "lap_p_in"), lap_p_tex);
	ev2::bind_buffer(dev, set, ev2::find_binding(layout, "ubo"), ubo, 0, sizeof(Uniforms));

	project_set = set;

	return 0;
}

int FluidSim::update(ev2::Device *dev)
{
	ev2::UploadContext uc = ev2::begin_upload(dev, sizeof(Uniforms), alignof(Uniforms));
	memcpy(uc.ptr, &uniforms, sizeof(Uniforms));
	ev2::BufferUpload up = {.size = sizeof(Uniforms)};
	uint64_t sync = ev2::commit_buffer_uploads(dev, uc, ubo, &up, 1);
	ev2::flush_uploads(dev);
	ev2::wait_complete(dev, sync);

	uint32_t group_size = 16;

	uint32_t gx = 1 + grid_w/group_size;
	uint32_t gy = 1 + grid_h/group_size;

	mean_subtractor->set_image(dev, lap_p_img);

	ev2::RecorderID rec = ev2::begin_commands(dev);

	ev2::cmd_bind_compute_pipeline(rec, nvs_advect);
	ev2::cmd_bind_descriptor_set(rec, advect_set);
	ev2::cmd_dispatch(rec, gx, gy, 1);

	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	ev2::cmd_bind_compute_pipeline(rec, nvs_diffuse);
	ev2::cmd_bind_descriptor_set(rec, diffuse_set);
	ev2::cmd_dispatch(rec, gx, gy, 1);

	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	ev2::cmd_bind_compute_pipeline(rec, nvs_pressure);
	ev2::cmd_bind_descriptor_set(rec, pressure_set);
	ev2::cmd_dispatch(rec, gx, gy, 1);

	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	mean_subtractor->record(rec);
	
	for (int i = 0; i < ((step == 0) ? 16 : 8); ++i) 
		pressure_solver->v_cycle(rec, dev, p_img, lap_p_img);

	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	ev2::cmd_bind_compute_pipeline(rec, nvs_project);
	ev2::cmd_bind_descriptor_set(rec, project_set);
	ev2::cmd_dispatch(rec, gx, gy, 1);

	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	//mean_subtractor->set_image(dev, p_img);
	//mean_subtractor->record(rec);

	ev2::end_commands(rec);

	 ++step;

	return 0;
}
void FluidSim::destroy(ev2::Device *dev)
{
	pressure_solver->destroy(dev);
	mean_subtractor->destroy(dev);

	ev2::destroy_image(dev, q_img_1);
	ev2::destroy_image(dev, q_img_2);

	ev2::destroy_image(dev, lap_p_img);
	ev2::destroy_image(dev, p_img);
	
	ev2::destroy_texture(dev, q_tex_1);
	ev2::destroy_texture(dev, q_tex_2);

	ev2::destroy_texture(dev, lap_p_tex);
	ev2::destroy_texture(dev, p_tex);
	
	ev2::destroy_buffer(dev, ubo);

	ev2::destroy_descriptor_set(dev, advect_set);
	ev2::destroy_descriptor_set(dev, diffuse_set);
	ev2::destroy_descriptor_set(dev, pressure_set);
	ev2::destroy_descriptor_set(dev, project_set);
}

struct FluidApp : public App
{
	std::unique_ptr<FluidSim> sim;
	std::unique_ptr<TextureViewerPanel> main_panel;
	std::unique_ptr<TextureViewerPanel> right_panel;
	std::unique_ptr<HeightmapViewerPanel> heightmap_panel;

	ev2::GraphicsPipelineID vector_field_pipe;
	ev2::DescriptorSetID vector_field_set;

	ev2::TextureID phi_tex;
	ev2::TextureID f_tex;

	bool m_stopped = false;
	uint64_t m_step = 0;

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

	sim.reset(new FluidSim);

	main_panel.reset(new TextureViewerPanel(this, 200, 0, 500, 500));
	right_panel.reset(new TextureViewerPanel(this, 700, 0, 500, 500, 
										  "pipelines/pressure_viz.yaml"));
	heightmap_panel.reset(new HeightmapViewerPanel());

	result = sim->init(dev, 512, 512);
	if (result)
		return result;

	phi_tex = ev2::create_texture(dev, sim->q_img_1, ev2::FILTER_BILINEAR);
	f_tex = ev2::create_texture(dev, sim->q_img_1, ev2::FILTER_BILINEAR);

	vector_field_pipe = ev2::load_graphics_pipeline(dev, "pipelines/vector_field.yaml");
	{
		ev2::DescriptorLayoutID layout = ev2::get_graphics_pipeline_layout(dev, vector_field_pipe);
		;
		vector_field_set = ev2::create_descriptor_set(dev, layout);
		ev2::bind_texture(dev, vector_field_set, ev2::find_binding(layout, "u_tex"), phi_tex);
	}

	result = main_panel->init(dev, f_tex); 
	if (result)
		return result;

	result = right_panel->init(dev, phi_tex); 
	if (result)
		return result;

	result = heightmap_panel->init(this, dev, sim->q_tex_1); 
	if(result)
		return result;

	return result;
}
int FluidApp::update()
{
	int result = EXIT_SUCCESS;
	uint64_t current_step = m_step;

	if ((result = App::update()))
		return result;

	ImGui::Begin("Editor");

	if (ImGui::Checkbox("Stopped", &m_stopped)) {
	}

	if (ImGui::Button("Step")) {
		++m_step;
	} else if (!m_stopped) {
		++m_step;
	}

	if (ImGui::Button("Reset")) {
		upload_img_data(dev, sim->p_img, sim->grid_w, sim->grid_h);
		upload_img_data(dev, sim->q_img_1, 1 + sim->grid_w, 1 + sim->grid_h);
		upload_img_data(dev, sim->q_img_2, 1 + sim->grid_w, 1 + sim->grid_h);

		sim->uniforms.cursor = sim->uniforms.cursor_prev = glm::vec2(1,0.5);
	}

	ImGui::SliderFloat("gravity", &sim->uniforms.gravity, -1, 1);

	ImGui::End();

	if (current_step != m_step) {
		if ((result = sim->update(dev)))
			return result;
	}

	if ((result = main_panel->update(dev)))
		return result;

	if ((result = right_panel->update(dev)))
		return result;

	if ((result = heightmap_panel->update(dev)))
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
	main_panel->render(dev);

	ev2::PassCtx pass = main_panel->begin_pass(dev);
	ev2::cmd_bind_gfx_pipeline(pass.rec, main_panel->rd.pipeline);
	ev2::cmd_bind_descriptor_set(pass.rec, main_panel->rd.desc_set);
	ev2::cmd_draw_screen_quad(pass.rec);

	//ev2::cmd_bind_gfx_pipeline(pass.rec, vector_field_pipe);
	//ev2::cmd_bind_descriptor_set(pass.rec, vector_field_set);

	//glDisable(GL_DEPTH_TEST);
	//glEnable(GL_BLEND);
	//glBlendEquation(GL_FUNC_ADD);
	//glBlendFunc(GL_ONE, GL_ONE);	

	//size_t count = sim->grid_w * sim->grid_h;
	//const uint32_t indices[] = {0, 1};
	//glDrawElementsInstanced(GL_LINES, 2, GL_UNSIGNED_INT, indices, count);

	ev2::SyncID pass_sync = ev2::end_pass(dev, pass);

	right_panel->render(dev);
	heightmap_panel->render(dev);
}
void FluidApp::destroy()
{
	main_panel->destroy(dev);
	right_panel->destroy(dev);
	heightmap_panel->destroy(dev);

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
