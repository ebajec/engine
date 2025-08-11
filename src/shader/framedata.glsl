#ifndef FRAMEDATA_GLSL
#define FRAMEDATA_GLSL

#define FRAMEDATA_BINDING 16

struct framedata_t
{
	mat4 p;
	mat4 v;
	mat4 pv;
	vec3 center;
	float t;
};

layout (binding = FRAMEDATA_BINDING) uniform Framedata
{
	framedata_t u_frame;
};

vec3 view_dir()
{
	mat4 m = transpose(u_frame.v);
	return -m[2].xyz;
}

#endif // FRAMEDATA_GLSL
