
#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"

#include "line.glsl"
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
	FragColor = vec4(0,0,0,0);

	JoinInfo join = create_join();

	vec2 p = in_pos - join.center;
	vec2 d = transpose(join.frame)*p;

	if (clip_join(p,d,join,type)){
		discard;
	}

	vec2 uv = compute_corner_uv(p,d,join,type);

	float fbuf = 0.0;
	uv.y = -fbuf + (1.0 + 2.*fbuf) * uv.y;

	uv.x *= (1.0 + 2.0*fbuf);


 	uv.x -= u_frame.t;

	FragColor += texture(u_tex,uv);
	FragColor.a = 0.5;

	if (uv.y < 0 || uv.y > 1)
		discard;
	//if (uv.y < 0 || uv.y > 1) {
	//	float f = uv.y > 1 ? (uv.y - 1.0)/fbuf : abs(uv.y)/fbuf;
	//	vec4 outline_color = vec4(1,1,1,1);

	//	FragColor = vec4(outline_color.rgb,(1-f));
	//}

}
