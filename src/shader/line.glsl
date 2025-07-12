struct line_uniforms_t
{
	uint count;
	float thickness;
};

layout (std140, binding = 0) uniform LineUniforms
{
	line_uniforms_t ubo;
};
