#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"
#include "line.glsl"

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec2 in_uv;
layout (location = 5) in float in_total_length;

layout (location = 10) flat in vec2 in_p0;
layout (location = 11) flat in vec2 in_p1;
layout (location = 12) flat in vec2 in_X;
layout (location = 13) flat in float in_delta0;
layout (location = 14) flat in float in_delta1;
layout (location = 15) flat in float in_join_angle0;
layout (location = 16) flat in float in_join_angle1;
layout (location = 17) flat in float in_s0;
layout (location = 18) flat in float in_s1;

layout (location = 0) out vec4 FragColor;


bool clip_round_join(vec2 p, vec2 dir, float thickness)
{
	float prod = dot(p,dir);

	if (prod > 0 && length(p) > ubo.thickness)
		return true;
	return false;
}

bool clip_round_join2(vec2 fp,vec2 uv)
{
	bool left = uv.x < 0.5;

	vec2 p = left ? fp - in_p0 : fp - in_p1;

	float prod = left ? dot(p,-in_X) : dot(p,in_X);

	if (prod > 0 && length(p) > ubo.thickness)
		return true;
	return false;
}

vec4 tex_color2(vec2 uv)
{
	return texture(u_tex,uv);
}

void main()
{
	bool left = in_uv.x < 0.5;
	float sgnX = left ? -1 : 1;

	vec2 center = left ? in_p0 : in_p1;  
	vec2 p = in_pos - center;
	vec2 dir = sgnX*in_X;

	bool outside = clip_round_join(p,dir,ubo.thickness); 

	if (outside)
		discard;

	float delta = left ? in_delta0 : in_delta1;
	float delta_sgn = sign(delta);
	float delta_abs = abs(delta);

	float join_angle = left ? in_join_angle0 : in_join_angle1;
	float s_start = left ? in_s0 : in_s1;

	mat2 m = mat2(dir,delta_sgn*vec2(dir.y,-dir.x));
	vec2 d = transpose(m)*p;

	float s = in_total_length;

	vec2 uv = in_uv;
	if (abs(d.x) < delta_abs && delta_abs < 4*ubo.thickness) {

		vec2 o = center - m*vec2(delta_abs,ubo.thickness); 
		vec2 diff = in_pos - o;
		vec2 r = normalize(diff);
		float prod = dot(r,m[1]);

		float tht1 = acos(clamp(prod,-1,1)); 
		float tht2 = join_angle; 

		float t = tht1/tht2;

		s = s_start + sgnX*t*delta_abs;
	}

	s *= 0.5/ubo.thickness;

	uv.x = s;

	float fsin = mod(s - 2*u_frame.t,4) < 2 ? 1 : 0;
	float fy = 10*abs(d.y);

	FragColor = 
		0.75*(1-0.5*fsin)*tex_color2(uv)
	 	+ vec4(0,0,fsin,fsin)
		+ vec4(fy,0,0,fy)
	;
}
