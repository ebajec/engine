#version 450 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"

struct tile_code_t
{
	uint face;
	uint zoom; 
	uint idx;
};

//------------------------------------------------------------------------------
// Frag

layout (binding = 0) uniform sampler2D u_tex;

layout (binding = 0) buffer Cameras
{
	mat4 matrices[];
};

layout (location = 0) in vec3 frag_pos;
layout (location = 1) in vec2 frag_uv;
layout (location = 2) in vec3 frag_normal;
layout (location = 3) in vec4 fcolor;
layout (location = 4) flat in tile_code_t in_code;

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

struct plane_t 
{
	vec3 n;
	float d;
};

struct frustum_t
{
	plane_t planes[6];
};

frustum_t camera_frustum(mat4 m)
{
	m = transpose(m);
	frustum_t frust;
	for (int i = 0; i < 6; ++i) {
		vec4 p;
		switch (i) {
			case 0: p = m[3] + m[0]; break; // left
			case 1: p = m[3] - m[0]; break; // right
			case 2: p = m[3] + m[1]; break; // bottom
			case 3: p = m[3] - m[1]; break; // top
			case 4: p = m[3] + m[2]; break; // near
			case 5: p = m[3] - m[2]; break; // far
		}

		float r_inv = 1.0f / length(vec3(p));
		frust.planes[i].n = -vec3(p) * r_inv;
		frust.planes[i].d = p.w * r_inv;
	}
	
	return frust;
}

bool cull_plane(vec3 v, plane_t pl)
{
	return dot(v,pl.n) < pl.d;
}

bool within_frustum(vec3 v, frustum_t frust)
{
	for (uint i = 0; i < 6; ++i) {
		bool res = cull_plane(v,frust.planes[i]);
		if (!res)
			return res;
	}
	return true;
}

void main()
{
	frustum_t frust = camera_frustum(matrices[0]);

	//if (!within_frustum(frag_pos, frust))
	// 	discard;

	vec2 uv = frag_uv;

	float r = length(uv - vec2(0.5));
	float f = exp(-16*pow(1-r,4));

	vec4 color = FACE_COLORS[in_code.face];
	color *= f;
	color.w = 0.5;

	vec3 sun = normalize(vec3(-1,-1,3));

	if (false) {
		vec3 dx = dFdx(frag_pos);
		vec3 dy = dFdy(frag_pos);
		vec3 n = normalize(cross(dx,dy));
		color = 0.8*fcolor*max(dot(n, sun),0) + 0.2*fcolor;
	}

	FragColor = color;
}
