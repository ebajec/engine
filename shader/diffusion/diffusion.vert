#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"

//--------------------------------------------------------------------------------------------------
// Vert

layout (binding = 0) uniform sampler2D u_tex;

layout (location = 0) in vec2 pos;
layout (location = 1) in vec2 uv;

layout (location = 0) out vec2 frag_pos;
layout (location = 1) out vec2 frag_uv;

void main()
{
	ivec2 size = textureSize(u_tex, 0);

	float aspect = float(size.x)/float(size.y);

	vec2 p = vec2(pos.x*aspect,pos.y);

	frag_pos = p;
	frag_uv = uv;

	gl_Position = u_frame.pv*vec4(p,0.0,1.0);
}

