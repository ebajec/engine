#ifndef EV2_GRID_SIM_H
#define EV2_GRID_SIM_H

#include "poisson_solver.h"

#include <ev2/resource.h>
#include <ev2/pipeline.h>

#include <glm/vec2.hpp>

#include <memory>

struct FluidParticle
{
	glm::vec2 pos;
	glm::vec2 vel;
};

struct GridFluidSim
{
	uint32_t grid_w;
	uint32_t grid_h;

	static constexpr uint32_t DIMS = 2;

	ev2::ImageID v_img_1[DIMS];
	ev2::ImageID v_img_2[DIMS];

	ev2::ImageID lap_p_img; // rhs of lap(phi) = f 
	ev2::ImageID p_img; // pressure
	
	ev2::ImageID bd_mask_img; // out of bounds mask: 0 = oob, 1 = inb
	ev2::ImageID air_mask_img; // non fluid mask: 0 = air, 1 = fluid

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
	ev2::BindingsID divergence_set;
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

#endif // EV2_GRID_SIM_H
