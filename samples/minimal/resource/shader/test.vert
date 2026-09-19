#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "core/shader/frame.glsl"

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

	vec2 res = vec2(u_frame.resolution);

	vec2 uv = 0.5 * (vec2(1) + pos); 

	vec2 aspect = min(vec2(res.y, res.x)/max(res.x, res.y), 1);

	pos *= aspect;

	out_pos = pos;
	out_uv = uv;
	gl_Position = u_view.pv * vec4(pos, 0, 1); 
}
