#version 460 core
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_shader_draw_parameters : require
#include "framedata.glsl"
#include "common.glsl"
#include "line.glsl"

struct DrawElementsIndirectCommand 
{
        uint  count;
        uint  instanceCount;
        uint  firstIndex;
        int  baseVertex;
        uint  baseInstance;
};

struct vert_data_t
{
	vec2 pos;
	float len;
	uint padding;
};

layout (location = 0) in uint id;

layout (std430, binding = 0) buffer Vertices
{
	vert_data_t data[];
};

layout (std430, binding = 2) buffer Commands
{
	DrawElementsIndirectCommand commands[];
};

layout (location = 0) out vec2 out_pos;
layout (location = 1) out vec2 out_uv;
layout (location = 5) out float out_total_length;

layout (location = 4) flat out float out_mid_weight;

layout (location = 10) flat out vec2 out_p0;
layout (location = 11) flat out vec2 out_p1;
layout (location = 12) flat out vec2 out_X;
layout (location = 17) flat out vec2 out_s;
layout (location = 14) flat out vec2 out_deltas;
layout (location = 16) flat out vec2 out_join_angles;

layout (location = 20) flat out uint dbg_draw;

const uint RIGHT_BIT = 0x1;
const uint TOP_BIT = 0x2;
const float ROOT2 = 1.41421356237;

float cross2(vec2 v1, vec2 v2)
{
	return v1.x*v2.y-v1.y*v2.x;
}
float compute_join_offset3(vec2 v1, vec2 v2, float w)
{
	// Assumption: |v1| = |v2| = 1
	float v1v2 = dot(v1,v2);

	float sinr2 = 1-v1v2;
	float cosr2 = 1+v1v2;

	float frac = sinr2/cosr2;

	return w*sign(cross2(v1,v2))*sqrt(frac);
}

const bool loop = false;

uint next_idx(uint idx) {
	return (idx + 1) < ubo.count ? idx + 1 : idx;

	uint tmp = (idx + 1 < ubo.count) ? 
		idx + 1 : 
		(loop ? 1 : idx);
	return tmp;
}

uint prev_idx(uint idx) {
	return idx > 0 ? idx - 1 : 0;

	uint tmp = (idx > 0) ? 
		idx - 1 :
		(loop ? ubo.count - 1 : 0);
	return tmp;
}

void main()
{
	uint draw = gl_DrawID;
	uint count = commands[draw].instanceCount;

	uint idx = gl_BaseInstanceARB + gl_InstanceID; 

	uint rel_idx = idx - gl_BaseInstanceARB; 
	dbg_draw = rel_idx;

	vert_data_t verts[2] = {data[idx],data[next_idx(idx)]}; 

	float s0 = verts[0].len;
	float s1 = verts[1].len;

	vec2 p0 = verts[0].pos;
	vec2 p1 = verts[1].pos;

	float len = s1 - s0;
	float inv_len = len > 1e-6 ? 1.0/len : 0;

	vec2 seg = p1 - p0;

	vec2 X = seg*inv_len;
	vec2 Y = vec2(X.y,-X.x);

	float width = ubo.thickness;
	
	//-------------------------------------------------------------------
	// Join offset at p0

	uint id_prev = prev_idx(idx);
	vert_data_t v_prev = data[id_prev]; 

	float delta0 = 0;
	vec2 p_prev = (v_prev.pos - p0)/(verts[0].len - v_prev.len);
	delta0 = -compute_join_offset3(p_prev,-X,width);

	//-------------------------------------------------------------------
	// Join offset at p1

	uint id_next = next_idx(idx + 1);
	vert_data_t v_next = data[id_next]; 
	
	float delta1 = 0;
	vec2 p_next = (v_next.pos - p1)/(v_next.len - verts[1].len);
	delta1 = compute_join_offset3(X,p_next,width);

	if (rel_idx == 0)
		delta0 = 0;
	if (rel_idx == count - 1)
		delta1 = 0;

	//-------------------------------------------------------------------
	// Compute vertex position

	bool top = bool(id & TOP_BIT);
	bool right = bool(id & RIGHT_BIT);

	float sgnY = top ? 1 : -1;
	float sgnX = right ? 1 : -1;

	vec2 p = right ? p1 : p0;
	float delta = right ? delta1 : delta0; 

	float dX = sgnY*delta;
	float dY = sgnY*width;

	if (limit_join(delta))
		dX = sgnX*width;

	vec2 vpos = p + dY*Y + dX*X;

	gl_Position = u_frame.pv * vec4(vpos,0,1);

	//-------------------------------------------------------------------
	// Adjust uvs to accomodate for joins 

	vec2 uv = vec2(right ? 1 : 0, top ? 0 : 1);
	uv.x += dX*inv_len;

	//-------------------------------------------------------------------
	// shader stage outputs
	
	// interpolated outputs
	out_pos = vpos;
	out_uv = uv;
	out_total_length = s0 + uv.x*(s1 - s0);

	out_mid_weight = 1.0/(1.0 + abs(delta1/delta0));
	// flat outputs, constant across segment
	out_p0 = p0;
	out_p1 = p1;
	out_X = X;
	out_deltas = vec2(delta0,delta1);
	out_join_angles = vec2(
		atan(abs(delta0),width),
		atan(abs(delta1),width));
	out_s = vec2(s0,s1);
}
