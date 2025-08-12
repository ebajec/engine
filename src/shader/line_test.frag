#version 460 core
#extension GL_GOOGLE_include_directive : require

//------------------------------------------------------------------------------
// Impl

#include "line.glsl"
#include "line_frag_defs.glsl"

const uint type = JOIN_TYPE_ROUND;

float signed_dist(vec2 p, vec2 d, JoinInfo join)
{
	return dot(p, vec2(in_X.y, -in_X.x));
}

void main()
{
	JoinInfo join = create_join();

	vec2 p = in_pos - join.center;
	vec2 d = transpose(join.frame)*p;

	FragColor = vec4(0,0,0,0);

	if (clip_join(p,d,join,type)) { 
		discard;
		FragColor = vec4(0,0.5,0,0.5);
	}

	if (signed_dist(p,d,join) < 0) {
		FragColor += vec4(0,0,0.5,0.5);
	}

	vec2 uv = compute_corner_uv(p,d,join,type);
 	uv.x -= u_frame.t;

	vec4 color = vec4(0.5,0,0,0.5);
	color = texture(u_tex, uv);

	vec4 palette[6] = {
		vec4(1,0,0,1),
		vec4(0,1,0,1),
		vec4(0,0,1,1),
		vec4(0,1,1,1),
		vec4(1,0,1,1),
		vec4(1,1,0,1)
	};

	FragColor = palette[dbg_draw%6];
	FragColor.a = 0.5;

	//FragColor += 0.1*texture(u_tex,uv);

	if (d.x > 0) {
		FragColor.g += 1;
	}
	if (join.sgnX < 0) {
		FragColor.b += 1;
	}
}
