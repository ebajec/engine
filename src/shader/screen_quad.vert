#version 430 core

//--------------------------------------------------------------------------------------------------
// Vert

layout (location = 0) in vec2 pos;
layout (location = 1) in vec2 uv;

layout (location = 0) out vec2 frag_pos;
layout (location = 1) out vec2 frag_uv;

void main()
{
	vec2 p = vec2(vec4(pos,0,1));

	frag_pos = p;
	frag_uv = uv;

	gl_Position = vec4(p,0.0,1.0);
}

