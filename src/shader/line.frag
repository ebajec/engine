#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"
#include "line.glsl"

layout (location = 0) in vec4 frag_color;
layout (location = 1) in vec4 frag_pos;
layout (location = 2) in vec2 in_uv;
layout (location = 3) flat in vec2 dir;
layout (location = 4) flat in vec2 center;

layout (location = 0) out vec4 FragColor;

vec4 dot_color(vec2 p, vec2 dir)
{
	return vec4(1);
}

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

vec4 tex_color(vec2 p, vec2 dir)
{
	vec2 uv = in_uv;
	return texture(u_tex,uv);
}

vec4 dash_color(vec2 p, vec2 dir)
{
	mat2 m = mat2(dir,vec2(dir.y,-dir.x));
	vec2 d = m*p;

	float r_circ = (0.5*ubo.thickness);
	float r_line = (ubo.thickness);

	float r_x = abs(d.x); 
	float r_y = abs(d.y); 

	float r_rect_y = (0.01);
	float r_rect_x = 0.1;

	bool in_rect = mod(r_x,r_rect_x) > 0.5*r_rect_x && r_y < r_rect_y;

	bool in_circle = length(p) < r_circ;
	bool in_line = r_y < r_line;

	bool inside = in_circle || in_rect; 

	float a = inside ? 0.8 : 0;

	float r = r_y > 0.8*ubo.thickness ? 1 : 0;

	if (d.x > 0) {
		r = length(d) > 0.8*ubo.thickness ? 1 : 0;
	}

	return vec4(
		in_rect ? 1 : 0,
		r,
		0,
		1
	);
}

bool clip_round_join(vec2 p, vec2 dir, float thickness)
{
	if (dot(p,dir) > 0 && length(p) > ubo.thickness)
		return true;
	return false;
}

void main()
{
	vec2 p = frag_pos.xy - center;

	bool outside = clip_round_join(p,dir,ubo.thickness); 

	if (outside)
		discard;

	vec4 color;

	//color = soft_color(p,dir);
	vec4 c_dash = dash_color(p,dir);
	color = tex_color(p,dir);

	color.a = max(color.a, 0.1);

	FragColor = color + c_dash;
}
