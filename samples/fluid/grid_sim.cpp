#include "grid_sim.h"

#include <ev2/utils/common.h>

#include <cstring>

int GridFluidSim::init(ev2::GfxContext *ctx, uint32_t w, uint32_t h)
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

	lap_p_img = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_32F, usage);
	ev2::set_image_name(ctx, lap_p_img, "lap_p_img");

	p_img = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_32F, usage);
	ev2::set_image_name(ctx, p_img, "p_img");

	bd_mask_img = ev2::create_image(ctx, grid_w, grid_h, 1, ev2::IMAGE_FORMAT_R8_UNORM, usage);
	ev2::set_image_name(ctx, bd_mask_img, "bd_mask");

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

	nvs_particles = ev2::load_compute_pipeline(ctx, "shader/nvs2_flip_advect");
	nvs_advect = ev2::load_compute_pipeline(ctx, "shader/nvs2_advect_semi_lagrange");
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
	ev2::bind_image(ctx, base_set, "bd_mask", bd_mask_img);
	ev2::bind_buffer(ctx, base_set, "ubo", ubo, 0, sizeof(Uniforms));
	ev2::flush_bindings(ctx, base_set);

	return 0;
}

int GridFluidSim::update_advect_set(ev2::GfxContext *ctx)
{
	ev2::BindingsID set = ev2::create_bindings(ctx, nvs_advect, 1, ev2::BINDING_MODE_STATIC);
	
	for (int i = 0; i < DIMS; ++i) {
		ev2::bind_image_indexed(ctx, set, "v_in", i, v_img_1[i]);
		ev2::bind_image_indexed(ctx, set, "v_out", i, v_img_2[i]);
	}

	ev2::flush_bindings(ctx, set);

	advect_set = set;

	return 0;
}

int GridFluidSim::update_diffuse_set(ev2::GfxContext *ctx)
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

int GridFluidSim::update_divergence_set(ev2::GfxContext *ctx)
{
	ev2::BindingsID set = ev2::create_bindings(ctx, nvs_divergence, 1, ev2::BINDING_MODE_STATIC);
	
	for (int i = 0; i < DIMS; ++i) {
		ev2::bind_image_indexed(ctx, set, "v_in", i, v_img_1[i]);
	}
	ev2::bind_image(ctx, set, "f_out", lap_p_img);
	ev2::flush_bindings(ctx, set);

	divergence_set = set;

	return 0;
}
int GridFluidSim::update_project_set(ev2::GfxContext *ctx)
{
	ev2::BindingsID set = ev2::create_bindings(ctx, nvs_project, 1, ev2::BINDING_MODE_STATIC);

	for (int i = 0; i < DIMS; ++i) {
		ev2::bind_image_indexed(ctx, set, "v_out", i, v_img_1[i]);
	}
	ev2::bind_texture(ctx, set, "p_in", p_tex);
	ev2::flush_bindings(ctx, set);

	project_set = set;

	return 0;
}

void GridFluidSim::step_sim(ev2::GfxContext *ctx)
{
	uint32_t group_size = 16;

	uint32_t gx = 1 + grid_w/group_size;
	uint32_t gy = 1 + grid_h/group_size;

	ev2::PassID pass = ev2::begin_compute_pass(ctx);
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
		ev2::cmd_use_image(pass, v_img_2[i], ev2::USAGE_SAMPLED_COMPUTE);
	}
	ev2::cmd_use_image(pass, lap_p_img, ev2::USAGE_STORAGE_WRITE_COMPUTE);

	ev2::cmd_bind_compute_pipeline(pass, nvs_divergence);
	ev2::cmd_bind_resources(pass, base_set);
	ev2::cmd_bind_resources(pass, divergence_set);
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

int GridFluidSim::update(ev2::GfxContext *ctx)
{
	ev2::UploadContext uc = ev2::begin_upload(ctx, sizeof(Uniforms), alignof(Uniforms));
	memcpy(uc.ptr, &uniforms, sizeof(Uniforms));
	ev2::BufferUpload up = {.size = sizeof(Uniforms)};
	uint64_t sync = ev2::commit_buffer_uploads(ctx, uc, ubo, &up, 1);
	ev2::flush_uploads(ctx);

	pressure_solver->set_inputs(ctx, p_img, lap_p_img, bd_mask_img);

	return 0;
}
void GridFluidSim::destroy(ev2::GfxContext *ctx)
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
	ev2::destroy_bindings(ctx, divergence_set);
	ev2::destroy_bindings(ctx, project_set);
}

