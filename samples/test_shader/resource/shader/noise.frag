#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "core/shader/core.glsl"
#include "test/shader/noise.glsl"

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec2 in_uv;

layout (location = 0) out vec4 out_color;

vec2 rot90(vec2 v)
{
	return vec2(-v.y, v.x);
}

vec2 get_u(vec2 p)
{
	vec2 u = normalize(vec2(p.y, -p.x));
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
				vec2 u = mix(get_u(p), get_u(ci), 0.5);

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

	if (false) {
	} else {
		float f = phase.y;
		//float f = sin(atan(phase.y, phase.x));
		out_color = vec4(vec3(f), 1);
	}
}
