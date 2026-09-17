#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "core/frame.glsl"

layout (location = 0) out vec2 out_pos;
layout (location = 1) out vec2 out_uv;

void main()
{
	vec2 positions[4] = {
		vec2(-1,-1),
	 	vec2(1,-1),
		vec2(-1,1),
	 	vec2(1,1)
	};

	vec2 pos = vec2(0);

	if (gl_VertexIndex < 3) {
		pos = positions[gl_VertexIndex];
	} else {
		pos = positions[1 + gl_VertexIndex % 3];
	}

	vec2 uv = 0.5 * (vec2(1) + pos); 

	out_pos = pos;
	out_uv = uv;
	gl_Position = u_view.pv * vec4(pos, 0, 1); 
}
