#ifndef DIFFUSION_GLSL
#define DIFFUSION_GLSL

layout (std140, binding = 1) uniform Uniforms
{
	vec2 cursor1;
	vec2 cursor2;

	float u_wave_c;
	float u_gradient;
	float u_conj_gradient;
	float u_laplacian;
	float u_decay;

	uint flags;
};


#endif // DIFFUSION_GLSL
