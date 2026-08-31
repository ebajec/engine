#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../core/frame.glsl"
#include "../core/jet_palette.glsl"
#include "fluid_particle.glsl"

layout (location = 0) out vec2 out_pos;
layout (location = 1) flat out vec2 out_center;
layout (location = 2) out vec4 out_color;
layout (location = 3) out vec2 out_vel;

layout (set = PER_DRAW_SET, binding = 0) readonly buffer Particles
{
	FluidParticle u_parts[];
};

layout (push_constant) uniform PC {
	mat3x2 u_world;
};

void main()
{
	const vec2 corners_ccw[4] = {
		vec2(-1,-1),
	 	vec2(1,-1),
		vec2(1,1),
	 	vec2(-1,1)
	};

	vec2 part_pos = u_parts[gl_InstanceIndex].pos;
	vec2 part_vel = u_parts[gl_InstanceIndex].vel;

	int vtx = gl_VertexIndex;

	float w = 0.0f;

	vec2 pos;
	if (vtx < 3) {
		pos = part_pos + w*corners_ccw[vtx];
	} else {
		pos = part_pos + w*corners_ccw[vtx - 1];
	}

	float f_vel = tanh(0.8*dot(part_vel, part_vel));

	vec3 base_color = jet_palette(f_vel);

	out_pos = pos;
	out_center = part_pos;
	out_color = mix(vec4(0.0), vec4(1), f_vel);
	out_vel = part_vel;

	gl_Position = u_view.pv * vec4(u_world * vec3(pos, 1) - vec2(1), 0, 1);
}
