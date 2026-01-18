#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"

layout (binding = 0) uniform sampler2D u_tex;

//--------------------------------------------------------------------------------------------------
// Vert

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

	ivec2 size = textureSize(u_tex, 0);
	float aspect = float(size.x)/float(size.y);

	pos.x *= aspect;

	frag_pos = pos;
	frag_uv = uv;

	gl_Position = u_view.pv * vec4(pos,0.0,1.0);
}

