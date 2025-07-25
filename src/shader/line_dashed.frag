#version 460 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"

#include "line.glsl"
#include "line_frag_defs.glsl"

const uint type = JOIN_TYPE_ROUND;

void main()
{
	JoinInfo join = create_join();

	vec2 p = in_pos - join.center;
	vec2 d = transpose(join.frame)*p;

	FragColor = vec4(0,0,0,0);

	if (clip_join(p,d,join,type)) 
		discard;

	vec2 uv = compute_corner_uv(p,d,join,type);
 	uv.x -= u_frame.t;

	float dash_scale = 3;
	float dash_weight = 0.2;
	float fx = mod(uv.x,(1 + dash_weight)*dash_scale) < dash_scale ? 1 : 0;

	vec4 color = vec4(0.5,0,0,0.5);

	FragColor = color * fx;

	if (d.x > 0)
		FragColor = vec4(0,0,1,0.5);

	vec4 palette[5] = {
		vec4(1,0,0,1),
		vec4(0,1,0,1),
		vec4(0,0,1,1),
		vec4(0,1,1,1),
		vec4(1,0,1,1)
	};

	FragColor = vec4(palette[uint(mod(dbg_draw,5))]) * (4.0/(1.0 + dbg_draw));
}
