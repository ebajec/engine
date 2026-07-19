#ifndef EV2_SAMPLE_POISSON_SOLVER_H
#define EV2_SAMPLE_POISSON_SOLVER_H

#include <ev2/context.h>
#include <ev2/resource.h>
#include <ev2/pipeline.h>

constexpr uint32_t MAX_PRESSURE_SOLVER_MIPS = 8;

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

	uint32_t N_max = 0;
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

struct MeanSubtractor
{
	ev2::ComputePipelineID accumulate;
	ev2::ComputePipelineID subtract_img;

	ev2::ImageID accumulator;
	ev2::ImageID out_img;

	struct ImageBindings {
		ev2::BindingsID accumulate;
		ev2::BindingsID subtract;
	};

	std::unordered_map<ev2::ImageID, ImageBindings> binding_map;

	uint32_t levels;
	uint32_t width, height;
	
	static constexpr uint32_t chunk_size = 8;
	static constexpr uint32_t group_size = 16;
	static constexpr uint32_t ratio = chunk_size * group_size;
	
	int init(ev2::GfxContext *ctx, uint32_t w, uint32_t h);
	int destroy(ev2::GfxContext *ctx);

	void record(ev2::PassID pass, ev2::ImageID img);
	void setup_bindings(ev2::GfxContext *ctx, ev2::ImageID img);
};

#endif
