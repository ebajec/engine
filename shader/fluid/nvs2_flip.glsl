#ifndef NVS_GLSL
#define NVS_GLSL

#extension GL_GOOGLE_include_directive : require

#include "fluid_particle.glsl"

#define GRID_WT 0.05

#define TIMESTEP 0.05
#define DELTA_X 1.5f
#define NU 0.0
#define RHO 1.0f

#define DIMS 2

layout (set = 0, r8, binding = 0) readonly uniform image2D solid_mask;

// x < 0 -> air
// 0 < x < 1 -> solid 
// 1 -> fluid
layout (set = 0, binding = 1, r8_snorm) uniform image2D bd_mask;
layout (set = 0, r32f, binding = 2) uniform image2D v_pre_proj[DIMS];
layout (set = 0, r32f, binding = 3) uniform image2D v_proj[DIMS];

layout (set = 0, binding = 4, std430) buffer Particles
{
	FluidParticle u_parts[];
};

struct VelocityCell
{
	float acc;
	float wt;
};

layout (set = 0, binding = 5, std430) buffer VelocityBuffer 
{
	uint s_offsets[DIMS];
	VelocityCell s_vel[];
};

// Goes to pressure solver
layout (set = 0, r32f, binding = 6) writeonly uniform image2D f_out;

// Comes from pressure solver
layout (set = 0, r32f, binding = 7) uniform readonly image2D p_in;

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
