#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require

layout (buffer_reference, std430, buffer_reference_align = 16) readonly buffer Values
{
	uint data[];
};

layout (std430, push_constant) uniform PC {
	uint u_count;
	uint u_bits;
	uint u_color;
	Values u_values;
};

