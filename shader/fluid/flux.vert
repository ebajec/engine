#version 460 core
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../core/frame.glsl"

#define DIMS 2

layout (location = 0) out vec4 out_color;

layout (push_constant, std430) uniform PC {
	ivec2 u_size;
	uint v_img[DIMS];
};

//      
//		  6       
// 0--2 5 |\.
// | / /| | \8
// |/ / | | /
// 1 3--4 |/ 
//		  7
//
//   Y----->
// X
// |
// |
// V

void main()
{
	uint inst = gl_InstanceIndex;

	ivec2 sx = ivec2(u_size.x + 1, u_size.y);
	ivec2 sy = ivec2(u_size.x, u_size.y + 1);

	int Nx = sx.x * sx.y;
	int Ny = sy.x * sy.y;

	vec2 d = 1.f/max(vec2(u_size), vec2(1.f));

	vec2 O;
	vec2 X;
	vec2 Y;

	float v;
	
	vec4 color;

	ivec2 idx;

	if (inst < Nx) {
		uint ix = inst;

		uint w = sx.x;
		uint quo = ix / w;
		idx = ivec2(ix - quo * w, quo);

		X = vec2(0, -1);
		Y = vec2(1, 0);

		O = d * vec2(float(idx.x), float(idx.y) + 0.5);

		v = texelFetch(u_textures[v_img[0]], idx, 0).r; 
	} else {
		uint iy = inst - Nx;

		uint w = sy.x;
		uint quo = iy / w;
		idx = ivec2(iy - quo * w, quo);

		X = vec2(1, 0);
		Y = vec2(0, 1);

		O = d * vec2(float(idx.x) + 0.5, float(idx.y));

		v = texelFetch(u_textures[v_img[1]], idx, 0).r; 
	}

	color = vec4(v, -v, 0, 1);
	v *= 1.f;

	float scale = min(d.x, d.y);

	float w = 0.1 * scale;
	float h = 0.25 * scale;

	h *= clamp(v, -5.f, 5.f);

	float arrow_w = 2*w;
	float arrow_h = 1.33*arrow_w * sign(v);

	vec2 pos;
	switch (gl_VertexIndex) 
	{
		case 0:
			pos = O - w*X;
			break;
		case 3:
		case 1:
			pos = O + w*X;
			break;
		case 2:
		case 5:
			pos = O - w*X + h*Y;
			break;
		case 4:
			pos = O + w*X + h*Y;
			break;
		case 6:
			pos = O - arrow_w * X + h*Y;
			break;
		case 7:
			pos = O + arrow_w * X + h*Y;
			break;
		case 8:
			pos = O + (arrow_h + h)*Y;
			break;
	}

	out_color = color;

	mat3x2 world = mat3x2(
		2,0, 0,2, -1,-1
	);

	gl_Position = u_view.pv * vec4(world * vec3(pos, 1), 0, 1);
}
