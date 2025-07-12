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

vec4 dash_color(vec2 p, vec2 dir)
{
	mat2 m = mat2(dir,vec2(dir.y,-dir.x));
	vec2 d = m*p;

	float r_circ = (0.1*ubo.thickness);
	float r_line = (ubo.thickness);
	float r_rect = (0.1*ubo.thickness);

	float r_x = abs((d.x)); 
	float r_y = abs(d.y); 

	bool in_rect = r_x > 0.3*ubo.thickness && r_y < r_rect;

	bool in_circle = length(p) < r_circ;
	bool in_line = r_y < r_line;

	bool inside = in_circle || in_line; 

	float a = inside ? 0.4 : 0;

	return vec4(
		in_rect ? 1 : 0,
		in_rect  ? 1 : 0,
		0,
		a
	);
}

vec4 soft_color(vec2 p, vec2 dir)
{
	mat2 m = mat2(dir,vec2(dir.y,-dir.x))/0.5*ubo.thickness;
	vec2 d = m*p;

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

void main()
{
	vec2 p = frag_pos.xy - center;

	if (dot(p,dir) > 0 && length(p) > ubo.thickness)
		discard;

	vec4 color;

	//color = soft_color(p,dir);
	color = dash_color(p,dir);

	color.a = max(color.a, 0.1);

	FragColor = color;
}
