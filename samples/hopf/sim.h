#ifndef HOPF_SIM_H
#define HOPF_SIM_H

#include "engine/resource/resource_table.h"
#include "engine/resource/material_loader.h"
#include "engine/resource/compute_pipeline.h"
#include "engine/resource/model_loader.h"
#include "engine/resource/buffer.h"

#include "engine/renderer/types.h"
#include "engine/renderer/renderer.h"

struct HopfSimPointData 
{
    glm::vec4 position;
    glm::vec4 color;
};

struct HopfSimTangentFrame
{
    glm::vec4 T; 
    glm::vec4 N;
    glm::vec4 B;
};

struct HopfSimVertex
{
    glm::vec4 position;
    glm::vec4 color;
    glm::vec4 normal;
};

struct HopfSimInstanceLine
{
    DrawCommand cmd;
    float width;
    float avgLength;
};

struct HopfSimParams
{
	uint32_t count;
	float anim_speed;
};

struct HopfSim
{
	ResourceTable *rt;

	ModelID ball_model;

	BufferID ubo;


	BufferID sphere_points; // HopfSimPointData[]

	BufferID ring_data; // HopfSimPointData[]
    BufferID ring_instances; // HopfSimInstanceLine[]
    BufferID ring_tangents; // HopfSimTangentFrame[]
	
	BufferID ring_mesh_verts; // HopfSimVertex[]
	BufferID ring_mesh_indices; // HopfSimVertex[]

	ComputePipelineID update_points;
	ComputePipelineID compute_fibers;
	ComputePipelineID compute_polylines[3];

	MaterialID ball_material;
	MaterialID ring_material;
};

struct HopfSimCreateInfo
{
	ResourceTable *rt;
	uint32_t max_count;
};

extern LoadResult hopf_sim_create(HopfSim **p_sim, HopfSimCreateInfo *ci);
extern void hopf_sim_destroy(HopfSim *p_sim);
extern void hopf_sim_update(HopfSim *sim);
extern void hopf_sim_draw_rings(HopfSim *sim, RenderContext const &ctx);
extern void hopf_sim_draw_balls(HopfSim *sim, RenderContext const &ctx);

#endif // HOPF_SIM_H

