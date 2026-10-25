#version 430 core
#include "core/shader/frame.glsl"
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require

layout (location = 0) in vec3 in_pos;
layout (location = 1) in vec3 in_normal;
layout (location = 0) out vec3 out_pos;
layout (location = 1) out vec4 out_color;

layout (buffer_reference, std430, buffer_reference_align = 16) readonly buffer Spectrum
{
	float data[];
};

layout (push_constant) uniform PC {
	Spectrum spec;
	int bins;
	int Fmax;
} pc;

uint pcg_hash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

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

	vec3 phi = vec3(0, f * t, 0);

	return c * cos(p.x * f - phi.x) * sin(p.y * f - phi.y) * cos(p.z * f - phi.x);
}

vec3 map(vec3 p, out vec4 color, out float dc)
{
	float h = 0.0;

	color = vec4(0,0,0,1);

	float sgn = 1;

	float scale = 32./float(pc.bins);//24.f/float(pc.bins);
	dc = pc.spec.data[0] * scale;

	float Fscale = 256;
	float Fstep = pc.Fmax / (Fscale * float(pc.bins));

	for (int i = 0; i < pc.bins; ++i) {
		float f = 6.28 * i * Fstep;
		float c = pc.spec.data[i];
		float x = basis(p, f, c) * scale;
		h += x; 

		color.rgb += 10*abs(x) * palette(int(f), pc.bins);

	}

	color.rgb *= abs(h - dc);

	p *= (1 + h);

	return vec3(p.x, p.y, p.z);
}

void main()
{
	float dc;
	vec4 color;
	vec3 pos = map(normalize(in_pos), color, dc);
	out_pos = pos;
	out_color = color;

	gl_Position = u_pass.pv * vec4(pos, 1); 
}

