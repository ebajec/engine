#ifndef SORT_PARTICLES_DEFS_GLSL
#define SORT_PARTICLES_DEFS_GLSL

#include "fluid_particle.glsl"

#include "../sort/sort_defs.glsl"

layout (push_constant) uniform PC
{
	SortPushConstantHeader sort;
	vec2 scale;
} pc;

uint spread2(uint x)
{
	x &= 0xFFFF;
	x = (x | (x << 8))  & 0x00FF00FF; 
	x = (x | (x << 4))  & 0x0F0F0F0F; 
	x = (x | (x << 2))  & 0x33333333; 
	x = (x | (x << 1))  & 0x55555555; 
	return x;
}

uint morton2(uint x, uint y)
{
	return spread2(x) | (spread2(y) << 1);
}

uint sort_get_key(FluidParticle part)
{
	uint ix = uint(part.pos.x * pc.scale.x);
	uint iy = uint(part.pos.y * pc.scale.y);

	return morton2(ix, iy);
}

#define SORT_DATA_TYPE FluidParticle

#include "../sort/sort_layout.glsl"

#endif // SORT_PARTICLES_DEFS_GLSL
