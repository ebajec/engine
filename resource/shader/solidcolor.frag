#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "core/shader/frame.glsl"

layout (location = 0) out vec4 FragColor;

void main() 
{
	FragColor = vec4(1,1,1,1);
}
