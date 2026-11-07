#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "core/shader/core.glsl"

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec2 in_uv;

layout (location = 0) out vec4 out_color;

uint pcg_hash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float random(uint x)
{
	return float(pcg_hash(x))/ 4294967296.0;
}

float random(vec2 v)
{
	uint x = floatBitsToUint(v.x) ^ (floatBitsToUint(v.y) * 277803737u);
	return random(x);
}

vec2 random2(vec2 v, uint seed)
{
	uint ux = floatBitsToUint(v.x);
	uint uy = floatBitsToUint(v.y);
	return vec2(
		random(seed + ux ^ (3u + uy * 277803737u)),
		random(seed + uy ^ (4u + ux * 277803737u)));
}

vec2 random2(ivec2 v, uint seed)
{
	uint ux = uint(v.x);
	uint uy = uint(v.y);
	return vec2(
		random(seed + ux ^ (3u + uy * 277803737u)),
		random(seed + uy ^ (4u + ux * 277803737u)));
}

float perlin(vec2 x, float s, uint seed)
{
	x /= s;
	ivec2 x00 = ivec2(floor(x)); 
	ivec2 x01 = ivec2(x00 + vec2(0,1)); 
	ivec2 x10 = ivec2(x00 + vec2(1,0)); 
	ivec2 x11 = ivec2(x00 + vec2(1,1)); 

	float g00 = dot(x - x00, normalize(vec2(1) - 2 * random2(x00, seed))); 
	float g01 = dot(x - x01, normalize(vec2(1) - 2 * random2(x01, seed))); 
	float g10 = dot(x - x10, normalize(vec2(1) - 2 * random2(x10, seed))); 
	float g11 = dot(x - x11, normalize(vec2(1) - 2 * random2(x11, seed))); 

	vec2 st = smoothstep(vec2(0), vec2(1), x - x00);

	return 
		(1.0 - st.y) * (g00 * (1.0 - st.x) + st.x * g10) +
		st.y * (g01 * (1.0 - st.x) + st.x * g11);
}

vec2 pgrad(vec2 x, float s, uint seed)
{
	x /= s;
	vec2 x00 = floor(x); 
	vec2 x01 = x00 + vec2(0,1); 
	vec2 x10 = x00 + vec2(1,0); 
	vec2 x11 = x00 + vec2(1,1); 

	vec2 g00 = vec2(1) - 2 * random2(x00, seed); 
	vec2 g01 = vec2(1) - 2 * random2(x01, seed); 
	vec2 g10 = vec2(1) - 2 * random2(x10, seed); 
	vec2 g11 = vec2(1) - 2 * random2(x11, seed); 

	vec2 st = smoothstep(vec2(0), vec2(1), x - x00);

	return normalize(
		(1.0 - st.y) * (g00 * (1.0 - st.x) + st.x * g10) +
		st.y * (g01 * (1.0 - st.x) + st.x * g11));
}


mat2 rot2(float tht)
{
	vec2 z = vec2(cos(tht), sin(tht));
	return mat2(
		vec2(z.x, z.y),
		vec2(-z.y, z.x)
	);
}

float fractal(vec2 x, float s, int octaves, uint seed)
{
	float n = 0;
	float scale = 1;
	float level = 1.6;
	mat2 R = rot2(6.28 * random(1));
	for (int i = 0; i < octaves; ++i) {
		x *= R;
		n += perlin(x, s, seed) / scale;
		s /= 2.0;
		scale *= level;
	}
	return n;
}

vec2 cmul(vec2 a, vec2 b)
{
	return vec2(a.x * b.x - a.y * b.y, a.y * b.x + a.x * b.y);
}

vec2 rot90(vec2 v)
{
	return vec2(-v.y, v.x);
}

vec2 get_u(vec2 p)
{
	vec2 u = normalize(vec2(p.y, -p.x));
	//vec2 u = pgrad(p, 4, 0);
	return u;
}

vec2 phasor(
	vec2 p, 
	float F,
	int cell_scale,
	float noise_scale,
	int octaves,
	float t)
{
	const float b = 1.1/float(cell_scale);
	const ivec2 center = cell_scale*ivec2(floor(p/float(cell_scale)));
	vec2 acc = vec2(0);

	vec2 z[2] = {
		vec2(
			fractal(p, noise_scale, octaves, 0),
			fractal(p, noise_scale, octaves, 277803737u)
		),
		vec2(
			fractal(p, noise_scale, octaves, 1u),
			fractal(p, noise_scale, octaves, 1u + 277803737u)
		)
	};

	for (int n = -1; n <= 1; ++n) {
		for (int m = -1; m <= 1; ++m) {
			for (int i = 0; i < 2; ++i) {
				ivec2 cell = center + cell_scale*ivec2(n,m);

				vec2 xi = cell_scale*(0.25 + 0.5*vec2(
					random(vec2(i + cell)),
					random(vec2(i + cell * 277803737u)))
				);
				vec2 ci = (vec2(cell) + xi);
				vec2 x = p - ci;
				vec2 u = mix(get_u(p), get_u(ci), 0.3);

				float a = exp(-3.14*b*b*dot(x,x)); 
				
				float phi = F * dot(x, u) - atan(z[i].y, z[i].x) - t;

				acc += a * length(z[i]) * vec2(cos(phi), sin(phi));
			}
		}
	}


	return acc;
}

layout (push_constant) uniform PC {
	int cell_scale;
	float F;
	float noise_scale;
	int octaves;
	float speed;
} pc;

vec3 srgb(vec3 c) { return pow(c, vec3(2.2)); }

#define GRADIENT(N, H, C, f) {                                     \
	vec3 col = C[0];                                               \
	for (int k = 1; k < N; ++k)                                    \
		col = mix(col, C[k], smoothstep(H[k-1], H[k], f));         \
	return srgb(col);                                              \
}

vec3 water_palette(float d)
{
	const int N = 4;
	const float H[N] = { 0.0, 0.08, 0.45, 1.0 };
	const vec3  C[N] = {
		vec3(0.20, 0.42, 0.45),  // shallow, greenish over rock
		vec3(0.07, 0.24, 0.38),  // shelf
		vec3(0.02, 0.08, 0.20),  // deep
		vec3(0.00, 0.01, 0.03),  // abyss, almost black
	};
	GRADIENT(N, H, C, d)
}

vec3 land_palette(float h) // h in [0, 1], 0 = shoreline
{
	const int N = 9;
	const float H[N] = { 0.0, 0.03, 0.08, 0.20, 0.38, 0.52, 0.66, 0.76, 0.84 };
	const vec3  C[N] = {
		vec3(0.13, 0.12, 0.11),  // wet dark rock at the waterline
		vec3(0.25, 0.23, 0.20),  // shingle / dark coastal stone
		vec3(0.24, 0.30, 0.17),  // coastal scrub
		vec3(0.20, 0.32, 0.13),  // lowland grass
		vec3(0.11, 0.22, 0.10),  // forest
		vec3(0.30, 0.28, 0.20),  // highland scrub / heath
		vec3(0.38, 0.36, 0.34),  // bare rock
		vec3(0.27, 0.26, 0.27),  // dark scree just below the snowline
		vec3(0.93, 0.95, 0.98),  // snow
	};
	GRADIENT(N, H, C, h)
}

vec3 terrain_palette(float f)
{
	if (f < 0)
		return water_palette(clamp(-f, 0.0, 1.0));
	return land_palette(clamp(f, 0.0, 1.0));
}

void main()
{
	vec2 p = screen_to_world(gl_FragCoord.xy).xy;

	vec2 phase = phasor(
		p, 
		pc.F,
		pc.cell_scale,
		pc.noise_scale,
		pc.octaves,
		pc.speed * ftime()
	);

	float f = phase.y - exp(-10*dot(p,p));
	//float f = sin(atan(phase.y, phase.x));

	float depth = 0.3;

	f -= depth;
	f *= 1.0/(1.0 - depth);
	out_color = vec4(terrain_palette(2*f), 1);
}
