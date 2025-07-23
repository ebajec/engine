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

layout (location = 0) out vec4 frag_pos;
layout (location = 1) out vec2 out_uv;

layout (location = 2) flat out vec2 dir;
layout (location = 3) flat out vec2 center;
layout (location = 4) flat out float out_delta;
layout (location = 6) flat out float out_y_sign;

layout (location = 5) out float out_total_length;

const uint SIDE_LEFT = 0;
const uint SIDE_RIGHT = 0x1;

const uint VERT_LOWER = 0x0;
const uint VERT_UPPER = 0x2;

const uint RIGHT_BIT = 0x1;
const uint TOP_BIT = 0x2;
const uint MID_BIT = 0x4;

const uint ID_LOWER_LEFT = 0;
const uint ID_LOWER_RIGHT = 1;
const uint ID_UPPER_LEFT = 2;
const uint ID_UPPER_RIGHT = 3;

vec2 normal_bisector(vec2 v1, vec2 v2)
{
	float d1 = length(v1);
	float d2 = length(v2);

	float denom = length(d1*v2 + d2*v1);

	if (denom < 0.00001)
		return v1;

	return (d1*v2 - d2*v1)/denom;
}

vec2 intersect_join(vec2 a, vec2 b, vec2 c, vec2 N)
{
	vec2 ba = a - b;
	vec2 bc = c - b;

	vec2 B = normal_bisector(ba,bc);

	float d = dot(b-a,B);

	if (d < 0.00001)
		return a + N + (b-a);

	float t = max(dot(b-a-N,B)/d,0);

	return a + N + t*(b-a);
}

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

	vec2 seg = p1 - p0;

	float len = length(seg);

	vec2 X = (seg)/len;
	vec2 Y = vec2(X.y,-X.x);

	bool top = bool(id & TOP_BIT);
	bool right = bool(id & RIGHT_BIT);
	bool mid = bool(id & MID_BIT);

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
	
	float mid_weight = 1.0/(1.0 + abs(delta1/delta0)); 

	vec2 p = right ? p1 : p0;
	vec2 uv = vec2(right ? 1 : 0, top ? 1 : 0);

	float dX = right ? len + sgnY*delta1 : sgnY*delta0;

	if (mid) {
		p = p0 + mid_weight*seg;
		dX = len*mid_weight;
	}
	float dY = sgnY*ubo.thickness;

	vec2 vpos = p0 + dY*Y + dX*X;

	gl_Position = u_frame.pv * vec4(vpos,0,1);

	//-------------------------------------------------------------------
	// Adjust uvs to reflect 

	uv.x = dX/len;
	//-------------------------------------------------------------------
	// shader stage outputs
	
	frag_pos = vec4(vpos,0,1);

	center = p;
	dir = sgnX*X;
	out_y_sign = sgnX;

	out_uv = uv;
	out_total_length = s0 + uv.x*(s1 - s0);

	out_delta = right ? abs(delta1) : abs(delta0);
}
