#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"

layout (location = 0) in vec3 frag_pos;
layout (location = 1) in vec2 frag_uv;
layout (location = 2) in vec3 frag_normal;

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
	vec4 color = vec4(1,0.4,0.4,1);

	FragColor = color; 
}
