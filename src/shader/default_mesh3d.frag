#version 430 core

struct framedata_t
{
	mat4 p;
	mat4 v;
	mat4 pv;
	float t;
};

layout (binding = 5) uniform Framedata
{
	framedata_t u_frame;
};

//--------------------------------------------------------------------------------------------------
// Frag

layout (binding = 0) uniform sampler2D u_tex;

layout (location = 0) in vec3 frag_pos;
layout (location = 1) in vec2 frag_uv;
layout (location = 2) in vec3 frag_normal;

layout (location = 0) out vec4 FragColor;

#define TWOPI 6.28318530718

vec4 diff(vec2 uv)
{
	float h = 0.005;
	vec4 sx0 = texture(u_tex,uv - vec2(h,0));
	vec4 sy0 = texture(u_tex,uv - vec2(0,h));
	vec4 sx1 = texture(u_tex,uv + vec2(h,0));
	vec4 sy1 = texture(u_tex,uv + vec2(0,h));

	vec4 ddx = (sx1 - sx0)/(2.0*h);
	vec4 ddy = (sy1 - sy0)/(2.0*h);

	return ddy;
}

void main()
{
	float t = u_frame.t;

	vec2 p = frag_uv;

	float theta = p.x*TWOPI;
	float r = p.y;

	vec2 uv = r*vec2(cos(theta),sin(theta));
	uv = 0.5*(uv + vec2(1.0));

	vec4 c0 = texture(u_tex,uv);

	vec4 c1 = diff(uv);

	float s = sin(t); s *= s;
	
	s = 0.0;

	FragColor = (1.0-s)*c0 + s*c1; 
}
