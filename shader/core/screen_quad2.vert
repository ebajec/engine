#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"

layout (set = PER_DRAW_SET, binding = 0) uniform sampler2D u_tex;

//--------------------------------------------------------------------------------------------------
// Vert

layout (location = 0) out vec2 frag_pos;
layout (location = 1) out vec2 frag_uv;

layout(constant_id = 0) const int PADDING = 4;

void main()
{
	uint id = gl_VertexIndex; 

	vec4 corners_ccw[4] = {
		vec4(-1,-1,0,0),
	 	vec4(1,-1,1,0),
		vec4(-1,1,0,1),
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

	ivec2 size = textureSize(u_tex, 0);
	vec2 h = vec2(1.f)/size;
	float pad = 100;

	uv = PADDING * (-h) + uv * (vec2(1) + 2*PADDING*h); 
	pos *= vec2(1) + 2*PADDING*h; 

	float aspect = float(size.x)/float(size.y);
	pos.x *= aspect;

	frag_pos = pos;
	frag_uv = uv;

	gl_Position = u_view.pv * vec4(pos,0.0,1.0);
}

