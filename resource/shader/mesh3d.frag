#version 460 core

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "core/shader/core.glsl"

layout (location = 0) in vec3 in_pos;
layout (location = 1) in vec2 in_uv;
layout (location = 2) in vec3 in_normal;
layout (location = 3) flat in uint in_tex_id;
layout (location = 4) flat in uint in_inst_id;

layout (location = 0) out vec4 out_color;
void main()
{
	vec4 color = texture(u_textures[in_tex_id], in_uv);
	out_color = color;
}

