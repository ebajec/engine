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
layout (location = 21) flat out vec2 out_hypot;
layout (location = 22) flat out uint out_endpoint;
layout (location = 20) flat out uint dbg_draw;

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

	float sinr2 = sqrt(1-v1v2);
	float cosr2 = sqrt(1+v1v2);

	float frac = sinr2/cosr2;

	float winding = sign(cross2(v1,v2));

	float delta = w*winding*frac;
	vec2 B = sinr2 < 1e-6 ? v1 : winding * (v1 - v2) / (ROOT2 * sinr2);

	JoinResult result;
	result.B = B;
	result.delta = delta;
	return result;
}

const bool loop = false;

uint next_idx(uint idx) {
	return (idx) < u_count ? idx + 1 : idx;

	uint tmp = (idx + 1 < u_count) ?
		idx + 1 :
		(loop ? 1 : idx);
	return tmp;
}

uint prev_idx(uint idx) {
	return idx > 0 ? idx - 1 : 0;

	uint tmp = (idx > 0) ?
		idx - 1 :
		(loop ? u_count - 1 : 0);
	return tmp;
}

void main()
{
	uint idx = gl_InstanceID;
	uint id = gl_VertexID;

	vert_data_t verts[4];

	uint id_prev = prev_idx(idx);
	vert_data_t v_prev = data[id_prev];

	uint id_next = next_idx(idx + 1);
	vert_data_t v_next = data[id_next];

	verts[0] = v_prev;
	verts[1] = data[idx];
	verts[2] = data[next_idx(idx)];
	verts[3] = v_next;

	dbg_draw = verts[1].padding;

	float s0 = verts[1].len;
	float s1 = verts[2].len;

	vec2 p0 = verts[1].pos;
	vec2 p1 = verts[2].pos;

	float len = s1 - s0;
	float inv_len = len > 1e-6 ? 1.0/len : 0;

	vec2 seg = p1 - p0;

	vec2 X = seg*inv_len;
	vec2 Y = vec2(X.y,-X.x);

	float scale = (u_frame.pv*vec4(p0,0,1)).w;
	float width = u_thickness;

	float len_prev = verts[1].len - verts[0].len;
	float len_next = verts[3].len - verts[2].len;

	bool top = bool(id & TOP_BIT);
	bool right = bool(id & RIGHT_BIT);

	float sgnY = top ? 1 : -1;
	float sgnX = right ? 1 : -1;

	uint endpoint = 
		RIGHT_ENDPOINT*uint((len_next < 0) || (idx == u_count)) | 
		LEFT_ENDPOINT*uint((len_prev < 0) || ((idx == 0)));

	//-------------------------------------------------------------------
	// Join offset at p0

	vec2 p_prev = (verts[0].pos - p0)/(len_prev);
	JoinResult j0 = compute_join_offset(p_prev,-X,width);
	float delta0 = -j0.delta;

	//-------------------------------------------------------------------
	// Join offset at p1

	vec2 p_next = (verts[3].pos - p1)/(len_next);
	JoinResult j1 = compute_join_offset(X,p_next,width);
	float delta1 = j1.delta;

	//-------------------------------------------------------------------
	// Compute vertex position

	vec2 p = right ? p1 : p0;
	float delta = right ? delta1 : delta0;

	float dX = sgnY*delta;
	float dY = sgnY*width;

	if (limit_join(delta,width) || 
		(bool(endpoint & LEFT_ENDPOINT) && sgnX < 0) ||
		(bool(endpoint & RIGHT_ENDPOINT) && sgnX > 0) 
	) {
		dX = sgnX*width;
	}
	delta0 = (idx == 0) ? width : delta0;

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
	out_width = width;

	// flat outputs, constant across segment
	out_mid_weight = 1.0/(1.0 + abs(delta1/delta0));

	out_p0 = p0;
	out_p1 = p1;
	out_X = X;
	out_deltas = vec2(delta0,delta1);
	out_join_angles = vec2(
		atan(abs(delta0),width),
		atan(abs(delta1),width));
	out_s = vec2(s0,s1);
	out_hypot = vec2(
		sqrt(delta0*delta0 + width*width),
		sqrt(delta1*delta1 + width*width));

	out_join_dir[0] = j0.B;
	out_join_dir[1] = j1.B;

	out_endpoint = endpoint;
}

