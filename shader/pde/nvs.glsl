#ifndef NVS_GLSL
#define NVS_GLSL

#define TIMESTEP 2.0
#define DELTA_X 0.5f
#define NU 0.0
#define RHO 1.f

layout (binding = 0) uniform ubo {
	vec2 u_cursor;
	vec2 u_cursor_prev;
	uint u_cursor_flags;
	float u_gravity;
};


float gaussian(vec2 x, vec2 c, float sigma)
{
	vec2 v = x - c;
	float r_sq = dot(v,v); 
	return sigma*exp(-sigma*sigma*r_sq);
}

#endif
