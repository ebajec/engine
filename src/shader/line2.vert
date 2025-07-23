#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"
#include "line.glsl"

struct vert_data_t
{
	vec2 pos;
	float len;
	float padding;
};

layout (location = 0) in uint id;

layout (std430, binding = 0) buffer Vertices
{
	vert_data_t data[];
};

layout (location = 0) out vec2 out_pos;
layout (location = 1) out vec2 out_uv;
layout (location = 5) out float out_total_length;

layout (location = 10) flat out vec2 out_p0;
layout (location = 11) flat out vec2 out_p1;
layout (location = 12) flat out vec2 out_X;
layout (location = 13) flat out float out_delta0;
layout (location = 14) flat out float out_delta1;
layout (location = 15) flat out float out_join_angle0;
layout (location = 16) flat out float out_join_angle1;
layout (location = 17) flat out float out_s0;
layout (location = 18) flat out float out_s1;

const uint RIGHT_BIT = 0x1;
const uint TOP_BIT = 0x2;

float compute_join_offset(vec2 v1, vec2 v2, float w)
{
	float B_denom = length(v2 - v1);

	vec2 B = B_denom > 1e-6 ? (v2 + v1)/B_denom : v1;
	float d = dot(v1,B);
	vec2 Y = vec2(-v1.y,v1.x);
	float t = d > 1e-6 ? max(dot(v1+w*Y,B)/d,0) : 1;

	return t - 1;
}

void main()
{
	uint idx = gl_InstanceIndex; 

	vert_data_t verts[2] = {data[idx],data[idx + 1]}; 

	float s0 = verts[0].len;
	float s1 = verts[1].len;

	vec2 p0 = verts[0].pos;
	vec2 p1 = verts[1].pos;

	float len = verts[1].len - verts[0].len;
	float inv_len = len > 1e-6 ? 1.0/len : 0;

	vec2 seg = p1 - p0;

	vec2 X = seg*inv_len;
	vec2 Y = vec2(X.y,-X.x);

	bool top = bool(id & TOP_BIT);
	bool right = bool(id & RIGHT_BIT);

	float sgnY = top ? 1 : -1;
	float sgnX = right ? 1 : -1;

	//-------------------------------------------------------------------
	// Join offset at p0

	uint id_prev = (idx > 0) ? idx - 1 : 0;
	vec2 v_prev = normalize(data[id_prev].pos - p0);
	float delta0 = -compute_join_offset(v_prev,-X,ubo.thickness);

	//-------------------------------------------------------------------
	// Join offset at p1

	uint id_next = (idx + 2 < ubo.count) ? idx + 2: idx;
	vec2 v_next = normalize(data[id_next].pos - p1);
	float delta1 = compute_join_offset(X,v_next,ubo.thickness);

	//-------------------------------------------------------------------
	// Compute offsets
	
	vec2 p = right ? p1 : p0;
	float delta = right ? delta1 : delta0; 

	float dX = sgnY*delta;
	float dY = sgnY*ubo.thickness;

	if (abs(delta) > 4*ubo.thickness)
		dX = ubo.thickness*sgnX;

	vec2 vpos = p + dY*Y + dX*X;

	gl_Position = u_frame.pv * vec4(vpos,0,1);

	//-------------------------------------------------------------------
	// Adjust uvs to reflect adjustments for joins 

	vec2 uv = vec2(right ? 1 : 0, top ? 0 : 1);
	uv.x += dX*inv_len;

	//-------------------------------------------------------------------
	// shader stage outputs
	
	out_pos = vpos;
	out_uv = uv;
	out_total_length = s0 + uv.x*(s1 - s0);

	out_p0 = p0;
	out_p1 = p1;
	out_X = X;
	out_delta0 = delta0;
	out_delta1 = delta1;
	out_join_angle0 = atan(abs(delta0),ubo.thickness);
	out_join_angle1 = atan(abs(delta1),ubo.thickness);
	out_s0 = s0 + abs(delta0);
	out_s1 = s1 - abs(delta1);
}
