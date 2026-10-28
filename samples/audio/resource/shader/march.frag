#version 430 core
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require

#include "core/shader/frame.glsl"

layout (location = 0) out vec4 out_color;

layout (buffer_reference, std430, buffer_reference_align = 16) readonly buffer Spectrum
{
	float data[];
};

layout (push_constant) uniform PC {
	Spectrum spec;
	int bins;
	int Fmax;
} pc;

#define MAX_BINS 32

//struct Coefficients
//{
//	float data[MAX_BINS];
//};

vec3 palette(int n, int N)
{
        const vec3 stops[5] = vec3[](
                vec3(0.051, 0.031, 0.188), // #0d0830 deep indigo
                vec3(0.180, 0.110, 0.490), // #2e1c7d violet blue
                vec3(0.310, 0.290, 0.839), // #4f4ad6 periwinkle
                vec3(0.639, 0.620, 0.969), // #a39ef7 lavender
                vec3(1.000, 1.000, 1.000)  // #ffffff
        );

        float t = N > 0 ? clamp(float(n) / float(N), 0.0, 1.0) : 1.0;

        float x = t * 4.0;
        int i = min(int(x), 3);
        vec3 c = mix(stops[i], stops[i + 1], x - float(i));

        return pow(c, vec3(2.2));
}

float basis(vec3 p, float f, float c)
{
	if (f == 0) {
		return c.x;
	}

	float t = ftime();
	float sin_t = sin(t);
	float cos_t = cos(t);

	vec3 phi = 3.14 * vec3(-0.5 * f * t, f * t, 0.5 * f * t);

	return c * cos(p.x * f - phi.x) * cos(p.y * f - phi.y) * cos(p.z * f - phi.x);
}

float func(vec3 p, out vec4 color, out float dc)
{
	float h = 0.0;

	color = vec4(0,0,0,1);

	float sgn = 1;

	const int bins = min(128, pc.bins);

	float scale = 24/float(bins);
	dc = pc.spec.data[0] * scale;

	float Fscale = 512;
	float Fstep = pc.Fmax / (Fscale * float(bins));

	for (int i = 0; i < bins; ++i) {
		float f = i * Fstep;
		float c = pc.spec.data[i];
		float x = basis(p, f, c) * scale;
		h += x; 

		color.rgb += 30*abs(x) * palette(int(i), bins);
	}

	color.rgb *= abs(h - dc);

	return h;
}
float sdf(vec3 p, out vec3 c)
{
	vec4 color;
	float dc;
	float f = func(p, color, dc);

	c = color.rgb;
	return length(p) - length(normalize(p) * (1 + f));
}

vec3 grad(vec3 p) {
	float h = 0.1;
	vec3 c;
	return vec3(
		sdf(p + vec3(h, 0, 0),c) - sdf(p - vec3(h, 0, 0),c), 
		sdf(p + vec3(0, h, 0),c) - sdf(p - vec3(0, h, 0),c), 
		sdf(p + vec3(0, 0, h),c) - sdf(p - vec3(0, 0, h),c) 
	) / (2 * h);
}

vec3 map(vec3 o, vec3 dir)
{
	vec3 p = o + 0.1 * dir;

	float eps = 0.01;
	float dmax = 10000;

	int max_its = 150;

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

		acc += 0.1/(5 + d*d) * color;

		if (abs(d) < eps || oob)
			break;
	}

	if (!oob) {
		vec3 n = grad(p);
		float f = clamp(dot(n, normalize(vec3(1,1,0))), 0, 0.8) + 0.2;

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

	vec3 color = map(o, dir);
	
	out_color = vec4(color, 1);
}
