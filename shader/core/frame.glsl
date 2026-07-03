#ifndef FRAMEDATA_GLSL
#define FRAMEDATA_GLSL

#define BINDLESS_SET 0 
#define PER_FRAME_SET 1
#define PER_PASS_SET 2
#define PER_DRAW_SET 3 

struct framedata_t
{
	uint t_seconds;
	float t_fract;
	float dt;
};

layout (set = PER_FRAME_SET, std140, binding = 0) uniform FrameData
{
	framedata_t u_frame;
};

struct viewdata_t
{
	mat4 p;
	mat4 v;
	mat4 pv;
	vec3 center;

	ivec2 resolution; 
};

layout (set = PER_PASS_SET, std140, binding = 0) uniform ViewData
{
	viewdata_t u_view;
};

float ftime()
{
	return float(u_frame.t_seconds) + u_frame.t_fract;
}

vec3 view_dir()
{
	mat4 m = transpose(u_view.v);
	return -m[2].xyz;
}

#endif // FRAMEDATA_GLSL
