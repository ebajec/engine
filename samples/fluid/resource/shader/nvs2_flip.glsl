#ifndef NVS_GLSL
#define NVS_GLSL

#extension GL_GOOGLE_include_directive : require

#include "fluid/shader/fluid_particle.glsl"

#define PARTS_PER_CELL 16
#define BETA 0.5
#define GRID_WT 0.2
#define TIMESTEP 0.02
#define DELTA_X 2.0f
#define RHO_MAX 100.f
#define RHO 1.0f
#define G 8

#define NU 0.0
#define DAMP 0.8

#define DIMS 2

// ppe
layout (set = 0, r8, binding = 0) readonly uniform image2D solid_mask;

// x < 0 -> air
// 0 < x < 1 -> solid 
// 1 -> fluid
layout (set = 0, binding = 1, r8_snorm) uniform image2D fill_mask;

layout (set = 0, binding = 2) uniform sampler2D solid_mask_tex;

layout (set = 0, r32f, binding = 3) uniform image2D v_pre_proj[DIMS];
layout (set = 0, r32f, binding = 4) uniform image2D v_proj[DIMS];

layout (set = 0, binding = 5, std430) buffer Particles
{
	FluidParticle u_parts[];
};

struct VelocityCell
{
	float acc;
	float wt;
};

layout (set = 0, binding = 6, std430) buffer VelocityBuffer 
{
	uint s_offsets[DIMS];
	VelocityCell s_vel[];
};

// Goes to pressure solver
layout (set = 0, r32f, binding = 7) writeonly uniform image2D f_out;

// Comes from pressure solver
layout (set = 0, r32f, binding = 8) uniform readonly image2D p_in;

layout (push_constant) uniform PC
{
	uint count;
	uint step;
	vec2 cursor1;
	vec2 cursor2;
	uint cursor_flags;
} pc;

float get_avg_vel(uint idx)
{
	float wt = s_vel[idx].wt;
	return (wt > 1e-3) ? s_vel[idx].acc / wt : 0;
}


#endif
