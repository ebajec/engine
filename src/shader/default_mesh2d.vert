#version 430 core

struct camera_t
{
	mat4 p;
	mat4 v;
	mat4 pv;
};

struct framedata_t
{
	camera_t camera;
	float t;
};

layout (binding = 5) uniform Framedata
{
	framedata_t u_frame;
};

//--------------------------------------------------------------------------------------------------
// Vert

layout (location = 0) in vec2 pos;
layout (location = 1) in vec2 uv;

layout (location = 0) out vec2 frag_pos;
layout (location = 1) out vec2 frag_uv;

void main()
{
	float t = u_frame.t;
	mat4 pv = u_frame.camera.pv;

	vec4 wpos = vec4(pos.x, pos.y, 0, 1);
	frag_pos = pos;
	frag_uv = uv;

	gl_Position = pv*wpos;
}

