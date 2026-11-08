#ifndef TERRAIN_GLSL
#define TERRAIN_GLSL

#include "test/shader/noise.glsl"

// Terrain occupies the square [-TERRAIN_WIDTH, TERRAIN_WIDTH]^2 in xy.
#define TERRAIN_WIDTH 10.0

const float TERRAIN_DEPTH    = -0.4;
const float TERRAIN_FARDEPTH = 0.2;
const float TERRAIN_GAIN     = 1.5;

float terrain_height(vec2 p, float noise_scale, int octaves)
{
	float f = fractal(p, noise_scale, octaves, 0);

	f -= TERRAIN_DEPTH;
	f *= 1.0/(1.0 - TERRAIN_DEPTH);

	float w =
		(1 - smoothstep(0, 1, abs(p.x/TERRAIN_WIDTH))) *
		(1 - smoothstep(0, 1, abs(p.y/TERRAIN_WIDTH)));

	return (TERRAIN_GAIN * f * w - TERRAIN_FARDEPTH)/(1 - TERRAIN_FARDEPTH);
}

// Thank you claude
//
// Upper bound on |grad terrain_height|.
//
// h = k * (f * w) - c, with k = GAIN / (1 - FARDEPTH), f = (n - DEPTH)/(1 - DEPTH)
//   |grad h| <= k * (|grad n| / (1 - DEPTH) + |f|max * |grad w|max)
//
// Perlin (unit gradients, cubic fade):  |grad P| <= 1.7910,  |P| <= sqrt(2)/2.
// Octave i has frequency 2^i / s and amplitude 2^-i, so every octave adds
// the same 1.7910 / s to the gradient bound: |grad n| <= octaves * 1.7910 / s,
// and |n| <= sqrt(2)/2 * (2 - 2^(1 - octaves)).
// The window w = (1 - S(|x|/W)) (1 - S(|y|/W)) has |grad w| <= 1.5 / W.
float terrain_lipschitz(float noise_scale, int octaves)
{
	const float LP = 1.7910;
	const float PMAX = 0.70711;

	float k = TERRAIN_GAIN / (1 - TERRAIN_FARDEPTH);

	float grad_n = float(octaves) * LP / noise_scale;
	float n_max = PMAX * (2 - exp2(1 - float(octaves)));
	float f_max = max(n_max - TERRAIN_DEPTH, n_max + TERRAIN_DEPTH) / (1 - TERRAIN_DEPTH);

	return k * (grad_n / (1 - TERRAIN_DEPTH) + f_max * 1.5 / TERRAIN_WIDTH);
}

#endif // TERRAIN_GLSL
