#ifndef FRAMEDATA_GLSL
#define FRAMEDATA_GLSL

#define VIEWDATA_BINDING 15
#define FRAMEDATA_BINDING 16

struct framedata_t
{
	mat4 p;
	mat4 v;
	mat4 pv;
	vec3 center;
	float t;
};

struct viewdata_t
{
	mat4 p;
	mat4 v;
	mat4 pv;
	vec3 center;
};

layout (std140, binding = FRAMEDATA_BINDING) uniform FrameData
{
	framedata_t u_frame;
};

layout (std140, binding = VIEWDATA_BINDING) uniform ViewData
{
	viewdata_t u_view;
};

vec3 view_dir()
{
	mat4 m = transpose(u_frame.v);
	return -m[2].xyz;
}

#endif // FRAMEDATA_GLSL
