#version 430 core

//--------------------------------------------------------------------------------------------------
// Frag

layout (binding = 0) uniform sampler2D u_tex;

layout (location = 0) in vec2 frag_pos;
layout (location = 1) in vec2 frag_uv;

layout (location = 0) out vec4 FragColor;

void main()
{
	vec4 color = texture(u_tex,frag_uv);
	FragColor = color;
}

