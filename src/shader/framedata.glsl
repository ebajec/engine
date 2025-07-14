#define FRAMEDATA_BINDING 5

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

