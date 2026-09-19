#version 460 core
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require

#include "core/shader/frame.glsl"
#include "test_sort/shader/test_defs.glsl"

layout (location = 0) out vec2 out_pos;
layout (location = 2) flat out uint out_idx;
layout (location = 3) flat out float out_w;

void main()
{
	const vec2 corners_ccw[4] = {
		vec2(0,0),
	 	vec2(1,0),
		vec2(1,1),
	 	vec2(0,1)
	};

	const float w = 1.f;

	vec2 pos;
	if (gl_VertexIndex < 3) {
		pos = w*corners_ccw[gl_VertexIndex];
	} else {
		pos = w*corners_ccw[gl_VertexIndex - 1];
	}

	out_pos = pos;
	out_idx = gl_InstanceIndex;
	out_w = w;

	gl_Position = u_view.pv * vec4(pos, 0, 1);
}
