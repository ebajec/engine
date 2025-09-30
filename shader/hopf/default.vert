#version 430 core
#extension GL_GOOGLE_include_directive : require

#include "framedata.glsl"

layout (std140, binding = 0) uniform Camera {
    mat4 view;
    mat4 proj;
	mat4 pv;
	vec4 cam_pos;
	vec4 cam_dir;
	float near;
	float far;
};

layout (std140, binding = 1) uniform Uniforms
{
	mat4 model;
	float scale;
};

layout (location = 0) in vec4 v_pos;
layout (location = 1) in vec4 v_color;
layout (location = 2) in vec4 v_normal;

layout (location = 0) out vec4 fcolor;
layout (location = 1) out vec3 fpos;
layout (location = 2) out vec3 fnormal;

void main() {
	float nscale = v_normal.w == 0 ? 1 : v_normal.w;

	// Apply geometry transformation
	vec4 position = model*vec4(scale*v_pos.xyz,1);
	vec4 normal = model*vec4(normalize(v_normal.xyz/nscale),0);

	fcolor = v_color;
	fpos = vec3(position);
	fnormal = normal.xyz;

	position = u_frame.pv*position;
    gl_Position = vec4(position);
}
