#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"

#include "line.glsl"
#include "line_frag_defs.glsl"

const uint type = JOIN_TYPE_MITRE;

void main()
{
	JoinInfo join = create_join();

	vec2 p = in_pos - join.center;
	vec2 d = transpose(join.frame)*p;

	FragColor = vec4(0,0,0,0);

	vec2 uv = compute_corner_uv(p,d,join,type);

 	uv.x -= u_frame.t;

	float fx = d.x > 0 ? length(p) : abs(d.y);
	fx /= 2*ubo.thickness;
	fx = mod(uv.x,4) < 2 ? 1 : 0;
	FragColor += texture(u_tex,uv);
	//FragColor += vec4(fx,0,0,fx);
}
