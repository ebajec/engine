#version 430 core
#extension GL_GOOGLE_include_directive : require

layout (binding = 0) uniform sampler2D u_tex;

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec2 in_uv;

layout (location = 0) out vec4 FragColor;

void main()
{
	ivec2 size = textureSize(u_tex, 0);

	uint L = 0;
	vec4 tex = textureLod(u_tex, in_uv, 3);

	vec3 rgb = 10*vec3(tex.z,0,0) + vec3(0.02);

	FragColor = vec4(rgb,1); 
}

