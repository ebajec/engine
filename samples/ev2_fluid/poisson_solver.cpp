
#include "poisson_solver.h"

#include <ev2/utils/common.h>
#include <ev2/utils/log.h>

#include <glm/vec2.hpp>
#include <cmath>
#include <string>
#include <bit>

void PoissonSolver::set_inputs(
	ev2::GfxContext *ctx, ev2::ImageID lhs, ev2::ImageID rhs, ev2::ImageID bd)
{
	ev2::reset_bindings(ctx, bindings1);
	ev2::bind_image(ctx, bindings1, "in_lhs", lhs); 
	ev2::bind_image(ctx, bindings1, "in_rhs", rhs); 
	ev2::bind_image(ctx, bindings1, "out_lhs", lhs); 

	ev2::bind_image_indexed(ctx, bindings1, "bd_mask", 0, bd); 
	for (uint32_t i = 0; i < N_max - 1; ++i) {
		ev2::bind_image_indexed(ctx, bindings1, "bd_mask", i + 1, bd_mips, i); 
	}
	ev2::flush_bindings(ctx, bindings1);

	ev2::reset_bindings(ctx, mipgen_bindings);
	ev2::bind_image_indexed(ctx, mipgen_bindings, "levels", 0, bd); 
	for (uint32_t i = 0; i < N_max - 1; ++i) {
		ev2::bind_image_indexed(ctx, mipgen_bindings, "levels", i + 1, bd_mips, i); 
	}
	ev2::flush_bindings(ctx, mipgen_bindings);

	input = {
		.lhs = lhs,
		.rhs = rhs,
		.bd = bd,
	};
}

int PoissonSolver::init(ev2::GfxContext *ctx, uint32_t w, uint32_t h)
{
	sim_w = w; 
	sim_h = h;

	if (!is_pow2(sim_w) || !is_pow2(sim_h))
		return -1;

	uint32_t log_dim = (uint32_t)std::bit_width(w) - 1; 

	N_max = std::min(
		std::max(log_dim ? log_dim - 1 : log_dim, 1U), 
		MAX_PRESSURE_SOLVER_MIPS
	);
	N = N_max;

	ev2::ImageUsageFlags usage = 
		ev2::IMAGE_USAGE_STORAGE_BIT | 
		ev2::IMAGE_USAGE_SAMPLED_BIT;

	R1 = ev2::create_image(ctx, sim_w, sim_h, 1, ev2::IMAGE_FORMAT_32F, usage, N); 
	ev2::set_image_name(ctx, R1, "R1");
	R2 = ev2::create_image(ctx, sim_w/2, sim_h/2, 1, ev2::IMAGE_FORMAT_32F, usage, N); 
	ev2::set_image_name(ctx, R2, "R2");

	bd_mips = ev2::create_image(ctx, sim_w/2, sim_h/2, 1, ev2::IMAGE_FORMAT_R8_UNORM, usage, N - 1);
	ev2::set_image_name(ctx, bd_mips, "BdMaskMips");

	tmp_lhs = ev2::create_image(ctx, sim_w, sim_h, 1, ev2::IMAGE_FORMAT_32F, usage);
	ev2::set_image_name(ctx, tmp_lhs, "tmp_lhs");

	multigrid_down = ev2::load_compute_pipeline(ctx, "shader/multigrid_down");
	multigrid_up = ev2::load_compute_pipeline(ctx, "shader/multigrid_up");
	mipgen = ev2::load_compute_pipeline(ctx, "shader/mipgen_r8");

	//-----------------------------------------------------------------------------
	// setup bindings

	bindings0 = ev2::create_bindings(ctx, multigrid_down, 0, ev2::BINDING_MODE_STATIC);
	ev2::bind_image(ctx, bindings0, "tmp_lhs", tmp_lhs); 

	for (uint32_t i = 0; i < N; ++i) {
		ev2::bind_image_indexed(ctx, bindings0, "R1", i, R1, i);
		ev2::bind_image_indexed(ctx, bindings0, "R2", i, R2, i);
	}
	ev2::flush_bindings(ctx, bindings0);

	bindings1 = ev2::create_bindings(ctx, multigrid_down, 1, ev2::BINDING_MODE_DYNAMIC);

	mipgen_bindings = ev2::create_bindings(ctx, mipgen, 0, ev2::BINDING_MODE_DYNAMIC);

	return EXIT_SUCCESS;
}

void PoissonSolver::destroy(ev2::GfxContext *ctx)
{
	ev2::destroy_image(ctx, R1);
	ev2::destroy_image(ctx, R2);
}

void PoissonSolver::record_setup(ev2::PassID pass)
{
	ev2::cmd_bind_compute_pipeline(pass, mipgen);
	ev2::cmd_bind_resources(pass, mipgen_bindings);

	ev2::cmd_use_image(pass, input.bd, ev2::USAGE_STORAGE_READ_COMPUTE);

	uint32_t w = sim_w, h = sim_h;

	for (int i  = 0; i < N - 1; ++i) {
		w >>= 1;
		h >>= 1;

		ev2::cmd_use_image(pass, bd_mips, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);

		uint32_t level = i;
		ev2::cmd_push_constant(pass, mipgen, 0, sizeof(uint32_t), &level);

		uint32_t gx = 1 + (w - 1)/16;
		uint32_t gy = 1 + (h - 1)/16;
		ev2::cmd_dispatch(pass, gx, gy, 1);
	}

	ev2::cmd_bind_resources(pass, bindings0);
	ev2::cmd_bind_resources(pass, bindings1);
}

void PoissonSolver::record_v_cycle(ev2::PassID pass)
{
	const uint32_t groups = 16;

	//-------------------------------------------------------------------
	// 'down' stage

	ev2::cmd_bind_compute_pipeline(pass, multigrid_down);

	ev2::cmd_use_image(pass, input.lhs, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);
	ev2::cmd_use_image(pass, input.rhs, ev2::USAGE_STORAGE_READ_COMPUTE);

	ev2::cmd_use_image(pass, bd_mips, ev2::USAGE_STORAGE_READ_COMPUTE);
	if (input.bd.is_valid())
		ev2::cmd_use_image(pass, input.bd, ev2::USAGE_STORAGE_READ_COMPUTE);

	ev2::cmd_use_image(pass, tmp_lhs, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE);

	// Downwards pass has 0 upto N passes inclusive: One pass of jacobi iterations
	// on original, then N smooth + downsample passes on the residuals

	constexpr uint its[] = {
		4, 4, 3, 3, 2, 2, 2, 2, 2
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

//------------------------------------------------------------------------------
// MeanSubtractor

int MeanSubtractor::init(ev2::GfxContext *ctx, uint32_t w, uint32_t h)
{
	accumulate = ev2::load_compute_pipeline(ctx, "shader/accumulate");
	subtract_img = ev2::load_compute_pipeline(ctx, "shader/subtract_img");

	width = w;
	height = h;
	levels = std::max((std::max(std::bit_width(w),std::bit_width(h)) - 1), 1);
	levels = 1 + (levels - 1) / (std::bit_width(ratio) - 1);

	ev2::ImageUsageFlags usage = 
		ev2::IMAGE_USAGE_STORAGE_BIT |
	    ev2::IMAGE_USAGE_SAMPLED_BIT;

	const uint32_t tw = 1 + (w - 1) / ratio;
	const uint32_t th = 1 + (h - 1) / ratio;

	static int name_idx = 0;

	accumulator = ev2::create_image(ctx, tw, th, 1, ev2::IMAGE_FORMAT_32F, usage, levels);
	ev2::set_image_name(ctx, accumulator, "accumulator");

	return 0;
}
int MeanSubtractor::destroy(ev2::GfxContext *ctx)
{
	ev2::destroy_image(ctx, accumulator);

	for (auto [img, bindings] : binding_map) {
		ev2::destroy_bindings(ctx, bindings.accumulate);
		ev2::destroy_bindings(ctx, bindings.subtract);
	}
	binding_map.clear();

	return 0;
}

void MeanSubtractor::setup_bindings(ev2::GfxContext *ctx, ev2::ImageID img)
{
	auto [it, inserted] = binding_map.emplace(img, ImageBindings{});

	if (inserted) {
		it->second = ImageBindings{
			.accumulate = ev2::create_bindings(ctx, accumulate, 0, ev2::BINDING_MODE_DYNAMIC),
			.subtract  = ev2::create_bindings(ctx, subtract_img, 0, ev2::BINDING_MODE_DYNAMIC),
		};
	}

	ev2::BindingsID accumulate_set = it->second.accumulate;
	ev2::BindingsID subtract_set = it->second.subtract;

	ev2::reset_bindings(ctx, accumulate_set);
	for (uint32_t i = 0; i < levels; ++i) {
		ev2::bind_image_indexed(ctx, accumulate_set, "img_in", i,
			i == 0 ? img : accumulator, //image 
			i ? i - 1 : 0 //level
		);
		ev2::bind_image_indexed(ctx, accumulate_set, "img_out", i, accumulator, i);
	}
	ev2::flush_bindings(ctx, accumulate_set);

	ev2::reset_bindings(ctx, subtract_set);
	ev2::bind_image(ctx, subtract_set, "img_in", accumulator, levels > 1 ? levels & 0x1 : 0);
	ev2::bind_image(ctx, subtract_set, "img_out", img);
	ev2::flush_bindings(ctx, subtract_set);

}

void MeanSubtractor::record(ev2::PassID pass, ev2::ImageID img)
{
	auto it = binding_map.find(img);
	
	if (it == binding_map.end()) {
		log_error("Image %d has no registered bindings.", img.id);
		return;
	}

	ImageBindings bindings = it->second;

	ev2::cmd_bind_compute_pipeline(pass, accumulate);
	ev2::cmd_bind_resources(pass, bindings.accumulate);

	struct Uniforms {
		uint32_t level;
	};

	uint32_t w = width;
	uint32_t h = height;

	ev2::cmd_use_image(pass, img, ev2::USAGE_STORAGE_READ_COMPUTE); 

	for (uint32_t i = 0; i < levels; ++i) {
		w = 1 + (w - 1)/ratio;
		h = 1 + (h - 1)/ratio;
		Uniforms pc = {
			.level = i
		};
		ev2::cmd_push_constant(pass, accumulate, 0, sizeof(pc), &pc);

		uint32_t gx = 1 + (w - 1)/group_size;
		uint32_t gy = 1 + (h - 1)/group_size;

		ev2::cmd_use_image(pass, accumulator, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE); 
		ev2::cmd_dispatch(pass, gx, gy, 1); 
	}

	ev2::cmd_use_image(pass, img, ev2::USAGE_STORAGE_READ_WRITE_COMPUTE); 
	ev2::cmd_bind_compute_pipeline(pass, subtract_img);
	ev2::cmd_bind_resources(pass, bindings.subtract);
	ev2::cmd_dispatch(pass, 1 + (width - 1)/group_size, 1 + (height - 1)/group_size, 1);
}


