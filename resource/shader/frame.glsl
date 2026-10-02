#ifndef FRAMEDATA_GLSL
#define FRAMEDATA_GLSL

#define BINDLESS_SET 0 
#define PER_FRAME_SET 1
#define PER_PASS_SET 2
#define PER_DRAW_SET 3 

struct framedata_t
{
	uint t_sec;
	float t_fract;
	float t;
	float dt;
	ivec2 resolution;
};

struct passdata_t
{
	mat4 p;
	mat4 v;
	mat4 pv;
	vec3 center;

	// Dimensions of viewport the pass renders into, in pixels. 
	vec2 resolution;
	vec2 inv_resolution;

	float near_plane;
	float far_plane;
};

layout (set = BINDLESS_SET, binding = 0) uniform sampler2D u_textures[];

layout (set = PER_FRAME_SET, std140, binding = 0) uniform FrameData
{
	framedata_t u_frame;
};

layout (set = PER_PASS_SET, std140, binding = 0) uniform PassData
{
	passdata_t u_pass;
};

// Temporary band-aid fix so that other shaders don't need to change
#define u_view u_pass

float ftime()
{
	return float(u_frame.t_sec) + u_frame.t_fract;
}

vec3 view_dir()
{
	mat4 m = transpose(u_view.v);
	return -m[2].xyz;
}

#endif // FRAMEDATA_GLSL
