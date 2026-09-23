
#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "core/shader/frame.glsl"

layout (location = 0) flat in uint in_idx;
layout (location = 0) out vec4 FragColor;

uint pcg_hash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

void main() 
{
	uint hash = pcg_hash(in_idx);

	vec4 c = unpackUnorm4x8(hash);

	FragColor = vec4(c.rgb,1);

}
