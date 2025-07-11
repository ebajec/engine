#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"

struct vert_data_t
{
	vec2 pos;
};

const uint ID_LOWER_LEFT = 0;
const uint ID_LOWER_RIGHT = 1;
const uint ID_UPPER_LEFT = 2;
const uint ID_UPPER_RIGHT = 3;

layout (location = 0) in uint id;

layout (std430, binding = 0) buffer Vertices
{
	vert_data_t data[];
};

layout (location = 0) out vec4 frag_color;
layout (location = 1) out vec4 frag_pos;

void main()
{
	uint idx = gl_InstanceIndex; 

	vert_data_t v[2] = {data[idx],data[idx + 1]};

	vec4 p0 = u_frame.pv*vec4(v[0].pos,0,1);
	vec4 p1 = u_frame.pv*vec4(v[1].pos,0,1);

	vec2 X = normalize(p1.xy - p0.xy);
	vec2 Y = vec2(X.y, -X.x);

	vec2 offset = vec2(0);
	vec4 p = vec4(0);

	float thickness = 0.1; 

	switch(id)
	{
	case ID_LOWER_LEFT:
		offset = thickness*(-X - Y);
		p = p0;
		break;
	case ID_LOWER_RIGHT:
		offset = thickness*(X - Y);
		p = p1;
		break;
	case ID_UPPER_LEFT:
		offset = thickness*(-X + Y);
		p = p0;
		break;
	case ID_UPPER_RIGHT:
		offset = thickness*(X + Y);
		p = p1;
		break;
	}

	vec4 pos = vec4(p.xy + offset, p.z, p.w);

	gl_Position = pos;

	frag_pos = pos;
	frag_color = vec4(1,0,0,0.5);
}

