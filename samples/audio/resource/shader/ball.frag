#version 430 core
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_ARB_derivative_control : require

layout (location = 0) in vec3 in_pos;
layout (location = 1) in vec4 in_color;
layout (location = 0) out vec4 out_color;

void main()
{
	vec3 dx = dFdx(in_pos);
	vec3 dy = dFdy(in_pos);

	float g = 1000*fwidthCoarse(1 - gl_FragCoord.z);

	vec3 color = in_color.rgb + vec3(1,0,0) * g * g; 

	out_color = vec4(color, 1);
}
