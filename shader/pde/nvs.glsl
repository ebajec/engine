#ifndef NVS_GLSL
#define NVS_GLSL

#define TIMESTEP 1.0
#define DELTA_X 1.0
#define NU 0.0
#define RHO 1.f

layout (binding = 0) uniform ubo {
	vec2 u_cursor;
	vec2 u_cursor_prev;
	uint u_cursor_flags;
};


float gaussian(vec2 x, vec2 c, float sigma)
{
	vec2 v = x - c;
	float r_sq = dot(v,v); 
	return sigma*exp(-sigma*sigma*r_sq);
}

#endif
