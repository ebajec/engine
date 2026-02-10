#ifndef DIFFUSION_GLSL
#define DIFFUSION_GLSL

layout (std140, binding = 1) uniform Uniforms
{
	vec2 cursor1;
	vec2 cursor2;
	uint flags;

	float u_s;

	float u_wave_c;
	float u_gradient;
	float u_conj_gradient;
	float u_laplacian;
	float u_decay;
};


#endif // DIFFUSION_GLSL
