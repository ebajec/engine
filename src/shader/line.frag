#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"
#include "line.glsl"

layout (location = 0) in vec4 frag_pos;
layout (location = 1) in vec2 in_uv;

layout (location = 2) flat in vec2 dir;
layout (location = 3) flat in vec2 center;
layout (location = 4) flat in float in_length;
layout (location = 5) in float in_total_length;
layout (location = 6) flat in uint instance_id;

layout (location = 0) out vec4 FragColor;

vec4 soft_color(vec2 p, vec2 dir)
{
	mat2 m = mat2(dir,vec2(dir.y,-dir.x));
	vec2 d = m*p;

	d /= 0.5*ubo.thickness;

	float fy = exp(-d.y*d.y);
	float fx = exp(-d.x*d.x);

	vec4 color = vec4(
		1,
		0,
		0,
		fy);

	return color;
}

vec4 tex_color()
{
	vec2 uv = in_uv;
	return texture(u_tex,uv);
}

bool border_color(vec2 d, float fwidth)
{
	float r_x = abs(d.x); 
	float r_y = abs(d.y); 

	float border_thres = (1.0 - fwidth)*ubo.thickness;

	float set_dist_sq = (d.x < 0 ? r_y*r_y : dot(d,d));

	bool in_border = set_dist_sq > border_thres*border_thres;

	return in_border;
}

vec4 equal_spaced_tex(float S, float L)
{
	float x = 2*in_uv.x*ubo.thickness;
	float y = in_uv.y;

	S *= 2*ubo.thickness;

	if (x < 0 || x > L)
		return vec4(0);

	float n = max(floor(L/S) - 1,1);
	float S_prime = L/n;


	x = mod(x + 0.5*(S - S_prime),S_prime);

	if (x > S)
		return vec4(0);

	return texture(u_tex,vec2(0.5*x/ubo.thickness,y));
}

vec4 tex_color2(float s)
{
	return texture(u_tex,vec2(0.5*s/ubo.thickness,in_uv.y));
}

bool equal_space_stripe(float x, float S, float L)
{
	if (x < 0 || x > L)
		return false;

	float n = max(floor(L/S),1);
	float S_prime = L/n;

	return abs(mod(x,S_prime) - S_prime*0.5) < S*0.25;
}

bool dash_color(vec2 d, float fspace, float fy)
{
	float s = 2*in_uv.x*ubo.thickness;
	float spacing = fspace*4*ubo.thickness;

	float r_x = abs(d.x); 
	float r_y = abs(d.y); 

	float r_rect_y = fy*ubo.thickness;

	bool stipple = equal_space_stripe(s,spacing,in_length);
	bool in_rect = stipple && r_y < r_rect_y;

	return in_rect;
}

bool dot_pattern(float x, float y, float S, float L, float r)
{
	if (x < 0 || x > L)
		return false;

	float n = max(floor(L/S),1);
	float S_prime = L/n;

	x = mod(x,S_prime) - S_prime*0.5;

	vec2 d = vec2(x,y);

	return length(d) < r;
}

bool dot_color(vec2 d, float fspace, float fr)
{
	float s = 2*in_uv.x*ubo.thickness;
	float spacing = fspace*4*ubo.thickness;
	float r_circ = fr*(ubo.thickness);

	bool in_circle = dot_pattern(s,abs(d.y),spacing, in_length, r_circ);
	return in_circle;
}


bool clip_round_join(vec2 p, vec2 dir, float thickness)
{
	float prod = dot(p,dir);

	if (prod > 0 && length(p) > ubo.thickness)
		return true;
	return false;
}

void main()
{
	vec2 p = frag_pos.xy - center;

	bool outside = clip_round_join(p,dir,ubo.thickness); 

	if (outside)
		discard;

	float s = 0.5*in_total_length/ubo.thickness;

	mat2 m = mat2(dir,vec2(dir.y,-dir.x));
	vec2 d = m*p;

	vec4 color = vec4(0,0,0,1);

	vec4 c_tex = tex_color();
	c_tex = equal_spaced_tex(1,in_length);

	vec4 c0 = tex_color2(in_total_length);

	float f_dash   = float(dash_color(d,0.5,0.05));
	float f_dot    = float(dot_color(d,1,0.7));
	float f_border = float(border_color(d,0.2));

	bool dash2 = (mod(s - u_frame.t,2) < 1.5) && abs(d.y) < ubo.thickness; 

	FragColor = 
		vec4(0,0,0,0.5) +
		f_border   * vec4(1,1,0,1)  
		//float(dash2)*vec4(0,0,1,1)
	;

	FragColor.b = d.x > 0 ? 1 : 0;
}
