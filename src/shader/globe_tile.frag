#version 450 core

#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"
#include "globe.glsl"

//------------------------------------------------------------------------------
// Frag

layout (location = 0) in vec3 in_pos;
layout (location = 1) in vec2 in_uv;
layout (location = 2) in vec3 in_normal;
layout (location = 3) in vec4 in_color;
layout (location = 4) flat in tile_code_t in_code;
layout (location = 7) flat in tex_idx_t in_tex_idx;

layout (location = 0) out vec4 FragColor;

const vec4 FACE_COLORS[6] = {
	vec4(1,0,0,1),
	vec4(0,1,0,1),
	vec4(0,0,1,1),

	vec4(0,1,1,1),
	vec4(1,0,1,1),
	vec4(1,1,0,1)
};

uint cube_face(vec3 v)
{
	float c[6] = {v.x,v.y,v.z,-v.x,-v.y,-v.z};

	uint argmax = 0;
	float max = -1e9;

	for (uint i = 0; i < 6; ++i) {
		if (c[i] >= max) {
			argmax = i;
			max = c[i];
		}
	}

	return argmax;
}

void main()
{
	vec2 uv = in_uv;

	float r = length(uv - vec2(0.5));
	float f = exp(-16*pow(1-r,4));

	vec3 uvw = vec3(in_uv, in_tex_idx.tex);

	float val = texture(u_tex_arrays[in_tex_idx.page], uvw).r;

	vec4 color = mix(vec4(0,0.5,0,1), vec4(0.4,0.45,0.5,1), clamp(20*val,0,1));

	vec3 sun = normalize(vec3(-1,-1,3));

	if (true) {
		vec3 dx = dFdx(in_pos);
		vec3 dy = dFdy(in_pos);
		vec3 n = normalize(cross(dx,dy));
		color = 0.8*color*max(dot(n, sun),0) + 0.2*color;
	}

	//vec4 ncolor = unpackUnorm4x8(64*in_tex_idx.tex*(in_tex_idx.page + 1));

	FragColor = mix(color,vec4(in_uv,0,1),0.0);
}
