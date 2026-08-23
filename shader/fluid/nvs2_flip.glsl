
#ifndef NVS_GLSL
#define NVS_GLSL

#extension GL_GOOGLE_include_directive : require

#include "fluid_particle.glsl"

#define GRID_WT 0.1

#define TIMESTEP 0.05
#define DELTA_X 1.0f
#define NU 0.0
#define RHO 1.f

#define DIMS 2

layout (set = 0, r8, binding = 0) readonly uniform image2D solid_mask;

// x < 0 -> air
// 0 < x < 1 -> solid 
// 1 -> fluid
layout (set = 0, binding = 1, r8_snorm) uniform image2D bd_mask;
layout (set = 0, r32f, binding = 2) uniform image2D v_pre_proj[DIMS];
layout (set = 0, r32f, binding = 3) uniform image2D v_proj[DIMS];

layout (set = 0, binding = 4, std430) buffer ParticlePositions
{
	vec2 u_part_pos[];
};
layout (set = 0, binding = 5, std430) buffer ParticleVelocity
{
	vec2 u_part_vel[];
};

layout (set = 0, binding = 6, std430) buffer VelocityBuffer 
{
	uint s_offsets[DIMS];
	float s_vel[];
};

// Goes to pressure solver
layout (set = 0, r32f, binding = 7) writeonly uniform image2D f_out;

// Comes from pressure solver
layout (set = 0, r32f, binding = 8) uniform readonly image2D p_in;

layout (push_constant) uniform PC
{
	uint count;
	uint step;
} pc;

#endif
