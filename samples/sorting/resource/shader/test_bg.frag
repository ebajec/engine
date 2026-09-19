#version 460 core
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require

layout (location = 0) in vec2 in_pos;
layout (location = 2) flat in uint in_idx;
layout (location = 3) flat in float in_w;

layout (location = 0) out vec4 out_color;

#include "test_sort/shader/test_defs.glsl"

void main()
{
	out_color = vec4(0,0,0,1);
}


