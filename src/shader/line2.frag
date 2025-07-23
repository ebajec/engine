#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"
#include "line.glsl"

layout (location = 0) in vec4 frag_pos;
layout (location = 1) in vec2 in_uv;

layout (location = 2) flat in vec2 dir;
layout (location = 3) flat in vec2 center;
layout (location = 4) flat in float in_delta;
layout (location = 6) flat in float in_y_sign;

layout (location = 5) in float in_total_length;

layout (location = 0) out vec4 FragColor;


bool clip_round_join(vec2 p, vec2 dir, float thickness)
{
	float prod = dot(p,dir);

	if (prod > 0 && length(p) > ubo.thickness)
		return true;
	return false;
}

vec4 tex_color2(float s)
{
	return texture(u_tex,vec2(0.5*s/ubo.thickness,in_uv.y));
}

void main()
{
	vec2 p = frag_pos.xy - center;
	bool outside = clip_round_join(p,dir,ubo.thickness); 

	if (outside)
		discard;

	mat2 m = mat2(dir,in_y_sign*vec2(dir.y,-dir.x));
	vec2 d = transpose(m)*p;

	vec2 o = center - m*vec2(in_delta,ubo.thickness); 

	float s = 0.5*in_total_length/ubo.thickness;

	vec4 c_test;
	if (abs(d.x) > in_delta) {
		c_test = vec4(0,1,0,0);
	} else {
		c_test = vec4(0,0,0,0);
	}

	vec2 r = frag_pos.xy - o;

	float fsin = length(r) < abs(in_delta) ? 1 : 0;
	//float fsin = pow(sin(s - u_frame.t),2);

	FragColor = 
		tex_color2(in_total_length)
	 + vec4(0,0,fsin,fsin);

	if (d.x > 0)
		FragColor += vec4(1,0,0,1);

	FragColor += c_test;
}
