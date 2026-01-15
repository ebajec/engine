#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"

//--------------------------------------------------------------------------------------------------
// Vert

layout (location = 0) in vec2 pos;
layout (location = 1) in vec2 uv;

layout (location = 0) out vec2 frag_pos;
layout (location = 1) out vec2 frag_uv;

void main()
{
	uint id = gl_VertexIndex; 

	vec2 pos;
	vec2 uv;

	switch (id) {
		case 0: 
			pos = vec2(-1,-1);
			uv = vec2(0, 0);
			break;

		case 1: 
			pos = vec2(1, -1);
			uv = vec2(1, 0);
			break;

		case 2: 
			pos = vec2(1,  1);
			uv = vec2(1, 1);
			break;

		case 3: 
			pos = vec2(-1, 1);
			uv = vec2(0, 1);
			break;
	}

	frag_pos = pos;
	frag_uv = uv;

	gl_Position = vec4(pos,0.0,1.0);
}

