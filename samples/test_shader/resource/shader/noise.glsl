#ifndef NOISE_GLSL
#define NOISE_GLSL

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
	float level = 2.0;
	mat2 R = rot2(6.28 * random(1));
	for (int i = 0; i < octaves; ++i) {
		x *= R;
		n += perlin(x, s, seed) / scale;
		s /= 2.0;
		scale *= level;
	}
	return n;
}

#endif // NOISE_GLSL
