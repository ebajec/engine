#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"

#include "line_frag_defs.glsl"

const uint type = JOIN_TYPE_ROUND;

float signed_dist(vec2 p, vec2 d, JoinInfo join)
{
	float sd = d.x > 0 ? length(p) : d.y;
	sd *= join.sgnX*join.delta_sgn/join.width;

	return sd;
}

void main()
{
	JoinInfo join = create_join();

	vec2 p = in_pos - join.center;
	vec2 d = transpose(join.frame)*p;

	FragColor = vec4(0,0,0,0);

	if (clip_join(p,d,join,type))
		discard;

	vec2 uv = compute_corner_uv(p,d,join,type);
	//uv = in_uv;
	//uv.x = clamp(in_total_length,in_s0,in_s1);

	//uv.x *= 0.5/join.width;

	uv.x += u_frame.t;

	float dot_spacing = 1;

	float rx = (mod(uv.x,dot_spacing) - dot_spacing*0.5)*2*join.width;
	float ry = (1.0 - 2*uv.y)*join.width;

	float r = sqrt(ry*ry + rx*rx);

	float cc = r < 0.5*join.width ? 2*r/join.width : 0;

	FragColor += vec4(0, cc, 0, cc);
}
