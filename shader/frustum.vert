#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"

layout (binding = 0) buffer Cameras
{
	mat4 matrices[];
};

layout (location = 0) out vec4 out_pos;

void main()
{
	uint idx = gl_InstanceID;

	mat4 pv = matrices[idx];
	mat4 inv = inverse(pv);

	uint vidx = gl_VertexID;

	vec4 pos = vec4(0,0,0,1);
	pos.x += bool(vidx & 0x1) ? -1 : 1;
	pos.y += bool(vidx & 0x2) ? -1 : 1;
	pos.z += bool(vidx & 0x4) ? -1 : 1;

	vec4 wpos = inv*pos;
	out_pos = wpos;

	gl_Position = u_frame.pv * wpos;
}
