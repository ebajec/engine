#version 430 core
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
	FragColor = vec4(0,0,0,0);

	JoinInfo join = create_join();

	vec2 p = in_pos - join.center;
	vec2 d = transpose(join.frame)*p;

	if (clip_join(p,d,join,type)){
		discard;
	}

	vec2 uv = compute_corner_uv(p,d,join,type);

	float fbuf = 0.2;
	uv.y = -fbuf + (1.0 + 2.*fbuf) * uv.y;

	uv.x *= (1.0 + 2.0*fbuf);

 	uv.x -= u_frame.t;

	FragColor += texture(u_tex,uv);

	if (uv.y < 0 || uv.y > 1) {
		float f = uv.y > 1 ? (uv.y - 1.0)/fbuf : abs(uv.y)/fbuf;
		vec4 outline_color = vec4(1,1,1,1);

		FragColor = vec4(outline_color.rgb,(1-f));
	}

}
