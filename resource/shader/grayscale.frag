#version 430 core
#extension GL_GOOGLE_include_directive : require

#include "core/shader/frame.glsl"

layout (set = PER_DRAW_SET, binding = 0) uniform sampler2D u_tex;

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec2 in_uv;

layout (location = 0) out vec4 FragColor;

void main()
{
	vec4 tex = textureLod(u_tex, in_uv, 0);

	FragColor = vec4(vec3(tex.r), 1);
}


