#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"

//--------------------------------------------------------------------------------------------------
// Vert

layout (location = 0) in vec2 pos;
layout (location = 1) in vec2 uv;

layout (location = 0) out vec2 frag_pos;
layout (location = 1) out vec2 frag_uv;

void main()
{
	uint id = gl_VertexIndex; 

	vec4 corners_ccw[4] = {
		vec4(-1,-1,0,0),
	 	vec4(1,-1,1,0),
		vec4(0,1,0,1),
	 	vec4(1,1,1,1)
	};

	vec2 pos;
	vec2 uv;

	if (id < 3) {
		pos = corners_ccw[id].xy;
		uv = corners_ccw[id].zw;
	} else {
		pos = corners_ccw[1 + id % 3 ].xy;
		uv = corners_ccw[1 + id % 3].zw;
	}

	frag_pos = pos;
	frag_uv = uv;

	gl_Position = vec4(pos,0.0,1.0);
}

