#version 430 core
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#include "core/shader/core.glsl"
#include "test/shader/terrain.glsl"

layout (location = 0) out vec4 out_color;

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

layout (push_constant) uniform PC {
	int cell_scale;
	float F;
	float noise_scale;
	int octaves;
	float speed;
	uint height_tex;
} pc;

float heightmap(vec2 p)
{
	vec2 size = vec2(textureSize(u_textures[pc.height_tex], 0));
	vec2 uv = (p + TERRAIN_WIDTH) / (2 * TERRAIN_WIDTH);
	uv = clamp(uv, 0.5 / size, 1 - 0.5 / size);
	return texture(u_textures[pc.height_tex], uv).r;
}

float sdf(vec3 p, vec3 dir, float lip, out float h, inout vec3 fog)
{
	h = heightmap(p.xy);

	float dh = (p.z - h) / (abs(dir.z) + length(dir.xy)*lip);
	float dc = max(abs(p.x), abs(p.y)) - TERRAIN_WIDTH;

	if (dh > dc) {
	if (p.z < 0.0)
		fog += max(dh*(1 + h)*vec3(0.1,0.3,0.5), vec3(0));
	else 
		fog += 0.02*clamp(1 - 6*p.z*max(h, 0.1),0,1)*vec3(0.01,0.01,0.02);
	}
	return max(dh, dc);
}

vec3 normal(vec2 p)
{
	float h = 2 * TERRAIN_WIDTH / float(textureSize(u_textures[pc.height_tex], 0).x);
	float zl = heightmap(p - vec2(h, 0));
	float zr = heightmap(p + vec2(h, 0));
	float zb = heightmap(p - vec2(0, h));
	float zt = heightmap(p + vec2(0, h));

	return normalize(vec3(vec2(zl - zr, zb - zt) / (2. * h), 1));
}

#define SUN vec3(5*vec2(cos(ftime()),sin(ftime())),3)

float light(vec3 o)
{
	const int max_its = 1024;
	const float eps = 0.0001;
	const float dmax = 50;

	vec3 n = normal(o.xy);
	vec3 dir = normalize(SUN - o);

	float lip = terrain_lipschitz(pc.noise_scale, pc.octaves);

	float h = 0;
	float t = 0;
	float d = 0;
	vec3 fog = vec3(0);
	for (int i = 0; i < max_its; ++i) {
		vec3 p = o + t * dir;
		d = sdf(p, dir, lip, h, fog);
		t += d;

		if (t > dmax)
			return clamp(dot(n, dir), 0.1, 1);
		else if (d < eps)
			return 0.1;
	}
	return 0.1;
}

vec3 map(vec3 o, vec3 dir)
{
	const int max_its = 1024;
	const float eps = 0.001;
	const float dmax = 50;

	vec3 fog = vec3(0.0);
	float lip = terrain_lipschitz(pc.noise_scale, pc.octaves);

	float h = 0;
	float t = 0;
	float d = 0;
	for (int i = 0; i < max_its; ++i) {
		vec3 p = o + t * dir;

		d = sdf(p, dir, lip, h, fog);
		t += d;

		if (t > dmax)
			break;
		else if (d < eps)
			break;
	}

	if (t > dmax)
		return vec3(0.1, 0.1, 0.15) + fog;


	vec3 p = o + t * dir;
	vec3 c = terrain_palette(p.z); 
 	float f = light(p);

	return f*c + min(fog, vec3(0.5));
}

void main()
{
	float aspect = max(u_pass.inv_resolution.x, u_pass.inv_resolution.y);
	vec2 screen = (gl_FragCoord.xy - u_pass.resolution / 2) * aspect;

	vec3 o = u_pass.center;
	vec3 dir = normalize(vec3(screen, -1) * mat3(u_pass.v));

	vec3 color = map(o, dir);
	
	out_color = vec4(color, 1);
}

