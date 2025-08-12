#ifndef LINE_GLSL
#define LINE_GLSL

#include "framedata.glsl"

layout (std140, binding = 0) uniform LineUniforms
{
	uint u_count;
	float u_thickness;
};

layout (binding = 1) uniform sampler2D u_tex;

const uint LEFT_ENDPOINT = 0x1;
const uint RIGHT_ENDPOINT = 0x2;

bool limit_join(float delta, float width)
{
	return abs(delta) > 6*width;
}
#endif // LINE_GLSL
