#version 430 core

#include "core/shader/core.glsl"

// Fractions of an octave
#define OFRACS 6
#define MAX_BINS 256

layout (location = 0) out vec4 out_color;

layout (set = EV2_BASE_USER_SET, binding = 0, std430) readonly buffer Spectrum {
	int bins;
	int Fmax;
	float coeffs[MAX_BINS];
} u_spec;

layout (set = EV2_BASE_USER_SET, binding = 1, std430) readonly buffer Palette {
	vec4 colors[MAX_BINS];
} u_palette;

float sd_torus(vec3 p, float R1, float R2)
{
	vec3 ax = vec3(R1 * normalize(p.xy), 0); 
	return length(p - ax) - R2;
}

vec2 cmul(vec2 a, vec2 b)
{
	return vec2(a.x * b.x - a.y * b.y, a.y * b.x + a.x * b.y);
}

float smin(float a, float b, float k) {
    return min(a, b) - k * log2(1.0 + exp2(-abs(a - b) / k));
}

float func(vec3 p, out vec4 color)
{
	float h = 0.0;

	color = vec4(0,0,0,1);

	float sgn = 1;

	// Palette is built over this same capped count, see main.cpp
	const int bins = min(128, u_spec.bins);

	float scale = 8/float(bins);
	float fscale = 1024;
	float roughness = 0.0;

	float t = ftime();

	float base = pow(2, 1/float(OFRACS));
	vec3 f = (u_spec.Fmax * pow(2, (1 - bins)/OFRACS) / fscale) * vec3(p);

	vec3 c2n[OFRACS];
	vec3 s2n[OFRACS];

	float factor = 1;
	for (int i = 0; i < OFRACS; ++i) {
		c2n[i] = cos(f * factor);
		s2n[i] = sin(f * factor);
		//factor *= base;
	}

	float sd = 1e10;
	float dc = u_spec.coeffs[0] * scale;

	vec2 dw = vec2(cos(t), sin(t));
	vec2 dz = vec2(cos(0.2), sin(0.2));
	vec2 z = vec2(1,0);

	float spacing = 1.0/bins;

	for (int i = 0; i < bins; ++i) {
		float c = u_spec.coeffs[i];

		int io = i % OFRACS;
		vec3 ci = c2n[io];

		float x = c * ci.x * ci.y * ci.z * scale;

		float c2 = u_spec.coeffs[bins - i - 1];
		vec3 off = 0.4*vec3(0,0,3 - 5*(1 + c2) * z.x);
		vec3 pi = 0.1*p; 

		float R1 = c2*spacing * (i);
		float R2 = mix(c2,c, 1 - roughness)*spacing;

		float torus1 = sd_torus(abs(pi + off), R1, R2); 
		float torus2 = sd_torus(vec3(pi -off), R1, R2); 
		float sd_i = min(torus1, torus2) - 0.3*R2 * h; 

		if (sd_i < sd)
			color.rgb += 10*abs(x) * u_palette.colors[i].rgb;

		sd = smin(sd_i, sd, 0.1 + 0.35 * dc);

		h += x;

		c2n[io] = ci * dw.x - s2n[io] * dw.y;
		s2n[io] = dw.x * s2n[io] + dw.y * ci;

		ci = c2n[io];

		c2n[io] = ci * ci - s2n[io] * s2n[io];
		s2n[io] = 2 * s2n[io] * ci;

		z = cmul(z, dz);
	}

	return sd;
}

float sdf(vec3 p, out vec3 c)
{
	vec4 color;
	float f = func(10*p, color);
	c = color.rgb;
	return f;
}

vec3 grad(vec3 p, float fc) {
	float h = 0.1;
	vec3 c;
	return vec3(
		sdf(p + vec3(h, 0, 0),c) - fc, 
		sdf(p + vec3(0, h, 0),c) - fc, 
		sdf(p + vec3(0, 0, h),c) - fc 
	) / (2 * h);
}

vec3 map1(vec3 o, vec3 dir)
{
	vec3 p = o;

	float eps = 0.01;
	float dmax = 10000;

	int max_its = 150;;

	bool oob = false;

	vec3 acc = vec3(0);

	float d = 0;

	vec3 color;

	int refinements = 0;

	int it = 0;
	for(; it < max_its; ++it) {
		d = sdf(p, color); 

		if (d < 0) {
			if (++refinements > 1) {
				break;
				acc += 0.1 * abs(d) * color;
			}
		}

		p += dir * d;
		oob = d > dmax;

		acc += 0.1/(1 + d ) * color;

		if (abs(d) < eps || oob)
			break;
	}

	if (!oob) {
		vec3 n = grad(p, d);
		return acc * length(n);
	}

	return acc;
}

void main()
{
	float aspect = max(u_pass.inv_resolution.x, u_pass.inv_resolution.y);
	vec2 screen = (gl_FragCoord.xy - u_pass.resolution / 2) * aspect;

	vec3 o = u_pass.center;
	vec3 dir = normalize(vec3(screen, -1) * mat3(u_pass.v));

	vec3 color = map1(2*o, dir);
	
	out_color = vec4(color, 1);
}
