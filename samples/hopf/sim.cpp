#include "sim.h"

#include "engine/resource/resource_table.h"
#include "engine/resource/material_loader.h"
#include "engine/resource/compute_pipeline.h"
#include "engine/resource/model_loader.h"
#include "engine/resource/buffer.h"

constexpr size_t FIBER_SIZE = 100;

LoadResult hopf_sim_create(HopfSim **p_sim, HopfSimCreateInfo *ci)
{
	std::unique_ptr<HopfSim> sim(new HopfSim{});

	ResourceTable *rt = ci->rt;

	//-----------------------------------------------------------------------------
	// Materials
	
	sim->ball_material = 
		material_load_file(rt, "material/hopf/spheres.yaml");
	if (!sim->ball_material)
		return RESULT_ERROR;
	sim->ring_material = 
		material_load_file(rt, "material/hopf/default.yaml");
	if (!sim->ring_material)
		return RESULT_ERROR;

	//-----------------------------------------------------------------------------
	// Compute shaders
	sim->update_points = 
		compute_pipeline_load(rt, "shader/spheres_transform.comp");
	if (!sim->update_points)
		return RESULT_ERROR;
	sim->compute_fibers = 
		compute_pipeline_load(rt, "shader/spheres_transform.comp");
	if (!sim->compute_fibers)
		return RESULT_ERROR;
	sim->compute_polylines[0] = 
		compute_pipeline_load(rt, "shader/polyline_0_tangents.comp");
	if (!sim->compute_polylines[0])
		return RESULT_ERROR;
	sim->compute_polylines[1] = 
		compute_pipeline_load(rt, "shader/polyline_1_tangents.comp");
	if (!sim->compute_polylines[1])
		return RESULT_ERROR;
	sim->compute_polylines[2] = 
		compute_pipeline_load(rt, "shader/polyline_2_tangents.comp");
	if (!sim->compute_polylines[2])
		return RESULT_ERROR;

	//-----------------------------------------------------------------------------
	// Buffers
	
	// HopfSimVertex[] | HopfSimLineData[] | 
	// HopfSimTangentFrame[] | HopfSimInstanceLine[]

	size_t ring_data_req_size = ci->max_count * (
		sizeof(HopfSimVertex));



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

