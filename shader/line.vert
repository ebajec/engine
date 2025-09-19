#version 460 core
#extension GL_GOOGLE_include_directive : require

//------------------------------------------------------------------------------
// Impl

#include "line.glsl"

struct vert_data_t
{
	vec2 pos;
	float len;
	uint padding;
};

struct metadata_t
{
    uint base;
    uint count;
};

layout (std430, binding = 0) buffer Vertices
{
	vert_data_t data[];
};

layout (std430, binding = 1) buffer Metadata
{
	metadata_t meta[];
};

layout (std430, binding = 2) buffer Indices
{
	uvec2 indices[];
};

layout (location = 0) out vec2 out_pos;
layout (location = 1) out vec2 out_uv;
layout (location = 5) out float out_total_length;
layout (location = 2) out float out_width;

layout (location = 4) flat out float out_mid_weight;
layout (location = 10) flat out vec2 out_p0;
layout (location = 11) flat out vec2 out_p1;
layout (location = 12) flat out vec2 out_X;
layout (location = 17) flat out vec2 out_s;
layout (location = 14) flat out vec2 out_deltas;
layout (location = 16) flat out vec2 out_join_angles;
layout (location = 18) flat out vec2 out_join_dir[2];
layout (location = 20) flat out uint dbg_draw;
layout (location = 21) flat out vec2 out_hypot;
layout (location = 22) flat out uint out_endpoint;
layout (location = 23) flat out vec2 out_bevel;

const uint RIGHT_BIT = 0x1;
const uint TOP_BIT = 0x2;
const float ROOT2 = 1.41421356237;

float cross2(vec2 v1, vec2 v2)
{
	return v1.x*v2.y-v1.y*v2.x;
}

struct JoinResult
{
	vec2 B;
	float delta;
};

JoinResult compute_join_offset(vec2 v1, vec2 v2, float w)
{
	// Assumption: |v1| = |v2| = 1
	float v1v2 = dot(v1,v2);

	float sin_tht = cross2(v1,v2);
	float cos_tht = dot(v1,v2);

	if (cos_tht < -0.999) {
		return JoinResult(vec2(0),0); 
	}

	float tan_tht_2 = sin_tht/(1+cos_tht);

	float delta = w*tan_tht_2;
	vec2 B = (abs(sin_tht) < 1e-4) ? 
		vec2(-v1.y,v1.x) : (sign(sin_tht)*normalize(v1 - v2));

	JoinResult result;
	result.B = B;
	result.delta = delta;
	return result;
}

uint next_idx(uint idx) {
	return (idx) < u_count ? idx + 1 : idx;
}

uint prev_idx(uint idx) {
	return idx > 0 ? idx - 1 : 0;
}

bool use_edges = true;

void main()
{
	uint idx = gl_InstanceID;

	vert_data_t verts[4];

	if (use_edges) {
		uvec2 e0 = indices[prev_idx(idx)];
		uvec2 e1 = indices[idx];
		uvec2 e2 = indices[next_idx(idx)];

		verts[0] = e1.x == e0.y ? data[e0.x] : data[e1.x];
		verts[1] = data[e1.x];
		verts[2] = data[e1.y];
		verts[3] = e1.y == e2.x ? data[e2.y] : data[e1.y];
	} else {
		uint prev = prev_idx(idx);
		uint next = next_idx(idx);

		verts[0] = data[prev];
		verts[1] = data[idx];
		verts[2] = data[next];
		verts[3] = data[next_idx(next)];
	}

	dbg_draw = verts[1].padding;

	float s0 = verts[1].len;
	float s1 = verts[2].len;

	vec2 p0 = verts[1].pos;
	vec2 p1 = verts[2].pos;

	float len = s1 - s0;
	float len_prev = verts[1].len - verts[0].len;
	float len_next = verts[3].len - verts[2].len;

	bool left_end = (len_prev < 1e-7) || (idx == 0);
	bool right_end = (len_next < 1e-7) || (idx == u_count);

	float inv_len = len > 1e-6 ? 1.0/len : 0;

	vec2 X = (p1 - p0)*inv_len;
	vec2 Y = vec2(X.y,-X.x);

	float width = u_thickness;

	uint id = gl_VertexID;

	bool top = bool(id & TOP_BIT);
	bool right = bool(id & RIGHT_BIT);

	float sgnY = top ? 1 : -1;
	float sgnX = right ? 1 : -1;

	//-------------------------------------------------------------------
	// Join offset at p0

	vec2 p_prev = left_end ? vec2(0) : (verts[0].pos - p0)/(len_prev);
	JoinResult j0 = compute_join_offset(p_prev,-X,width);
	float delta0 = -j0.delta;

	//-------------------------------------------------------------------
	// Join offset at p1

	vec2 p_next = right_end ? vec2(0) : (verts[3].pos - p1)/(len_next);
	JoinResult j1 = compute_join_offset(X,p_next,width);
	float delta1 = j1.delta;

	delta0 = (idx == 0) ? width : delta0;

	//-------------------------------------------------------------------
	// Compute vertex position

	float mid = 1.0/(1.0 + abs(delta1/delta0));
	
	vec2 p = right ? p1 : p0;
	float delta = right ? delta1 : delta0;

	float dX = right ? 
		max(sgnY*delta,-(len * (1.0 - mid))) : 
		min(sgnY*delta, len*mid);

	float dY = sgnY*width;

	if (limit_join(delta,width) || 
		(left_end && sgnX < 0) ||
		(right_end && sgnX > 0) 
	) {
		dX = sgnX*width;
	}
	vec2 vpos = p + dY*Y + dX*X;

	gl_Position = u_frame.pv*vec4(vpos,0,1);

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
	out_width = width;

	// flat outputs, constant across segment
	out_mid_weight = abs(delta0) > 0 ? mid : 0;

	out_p0 = p0;
	out_p1 = p1;
	out_X = X;
	out_deltas = vec2(delta0,delta1);
	out_join_angles = vec2(
		atan(abs(delta0),width),
		atan(abs(delta1),width));
	out_s = vec2(s0,s1);

	float w_sq = width * width;
	float d0_sq = delta0 * delta0;
	float d1_sq = delta1 * delta1;

	out_hypot = vec2(
		sqrt(d0_sq + w_sq),
		sqrt(d1_sq + w_sq));
	out_bevel = vec2(
		sqrt(w_sq*(1 - d0_sq/(w_sq + d0_sq))),
		sqrt(w_sq*(1 - d1_sq/(w_sq + d1_sq)))
	);

	out_join_dir[0] = j0.B;
	out_join_dir[1] = j1.B;

	out_endpoint = LEFT_ENDPOINT*uint(left_end) | RIGHT_ENDPOINT*uint(right_end);
}

