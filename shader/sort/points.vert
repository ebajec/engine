#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../core/frame.glsl"
#include "test_defs.glsl"

layout (location = 0) out vec2 out_pos;
layout (location = 1) flat out vec2 out_center;
layout (location = 2) flat out uint out_idx;
layout (location = 3) flat out vec2 out_w;

void main()
{
	const vec2 corners_ccw[4] = {
		vec2(-1,-1),
	 	vec2(1,-1),
		vec2(1,1),
	 	vec2(-1,1)
	};

	uint maxval = 1 << u_bits;

	const vec2 w = vec2(0.5f/float(u_count), 0.5/float(maxval));

	int idx = gl_InstanceIndex;
	vec2 center = vec2(
		(float(idx) + 0.5)/float(u_count),
		(float(u_values.data[idx]) + 0.5)/float(maxval));

	vec2 pos;

	if (gl_VertexIndex < 3) {
		pos = center + w*corners_ccw[gl_VertexIndex];
	} else {
		pos = center + w*corners_ccw[gl_VertexIndex - 1];
	}

	out_pos = pos;
	out_center = center;
	out_idx = gl_InstanceIndex;
	out_w = w;

	vec4 scr_cen = u_view.pv * vec4(center, 0, 1);
	vec4 scr_pos = u_view.pv * vec4(pos, 0, 1);

	vec2 d = scr_pos.xy - scr_cen.xy;

	vec2 min_w = vec2(2.f)/(u_frame.display_res);
	bvec2 too_small = lessThan(abs(d), min_w);

	scr_pos.x = too_small.x ? (scr_cen.x + sign(d.x) * min_w.x) : scr_pos.x;
	scr_pos.y = too_small.y ? (scr_cen.y + sign(d.y) * min_w.y) : scr_pos.y;

	gl_Position = scr_pos;
}
