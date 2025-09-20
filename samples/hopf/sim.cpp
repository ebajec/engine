#include "sim.h"

LoadResult hopf_sim_create(HopfSim **p_sim, HopfSimCreateInfo *ci)
{
	std::unique_ptr<HopfSim> sim(new HopfSim{});

	ResourceTable *rt = ci->rt;

	sim->update_points = load_shader_file(rt, "shader/spheres_transform.comp");
	sim->compute_fibers = load_shader_file(rt, "shader/spheres_transform.comp");
	sim->compute_polylines[0] = load_shader_file(rt, "shader/polyline_0_tangents.comp");
	sim->compute_polylines[1] = load_shader_file(rt, "shader/polyline_1_tangents.comp");
	sim->compute_polylines[2] = load_shader_file(rt, "shader/polyline_2_tangents.comp");

	return RESULT_SUCCESS;
}

void hopf_sim_destroy(HopfSim *p_sim)
{
}

void hopf_sim_update(HopfSim *sim)
{
}

void hopf_sim_draw_rings(HopfSim *sim, RenderContext const &ctx)
{
}

void hopf_sim_draw_balls(HopfSim *sim, RenderContext const &ctx)
{
}

