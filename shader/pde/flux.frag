#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "../core/frame.glsl"

layout (location = 0) in vec4 in_color;

layout (location = 0) out vec4 FragColor;

void main()
{
	FragColor = vec4(in_color);
}
