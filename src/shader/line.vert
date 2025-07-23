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

layout (location = 4) flat out float out_length;

layout (location = 5) out float out_total_length;

layout (location = 6) flat out uint instance_id;

const uint SIDE_LEFT = 0;
const uint SIDE_RIGHT = 0x1;

const uint VERT_LOWER = 0x0;
const uint VERT_UPPER = 0x2;

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

void main()
{
	uint idx = gl_InstanceIndex; 

	vert_data_t v[2] = {data[idx],data[idx + 1]};

	vec4 p0 = vec4(v[0].pos,0,1);
	vec4 p1 = vec4(v[1].pos,0,1);
	vec2 dp = p1.xy - p0.xy;

	float len = length(dp);

	vec2 X_abs = dp/len;

	vec2 X = X_abs;
	vec2 Y = vec2(X.y, -X.x);

	vec2 uv = vec2(1,1);

	uint vert = id & 0x2;
	if (vert == VERT_LOWER) {
		Y = -Y;	
		uv.y = 0;
	} else if (vert == VERT_UPPER) {
		Y = Y;
		uv.y = 1;
	}
	Y *= ubo.thickness;

	vec2 offset = vec2(0);
	vec4 p = vec4(0);
	vec4 a = vec4(0); 
	vec4 b = vec4(0);

	float s0 = v[0].len;
	float s1 = v[1].len;
	float ds = s1 - s0;
	float s = 0;

	uint side = id & 0x1;

	if (side == SIDE_LEFT) {
		uint id_prev = (idx > 0) ? idx - 1 : 0;
		a = p1;
		p = p0;
		b = vec4(data[id_prev].pos,0,1);

		X *= -1;
		uv.x = 0;

		s = s0;
	} 
	else if (side == SIDE_RIGHT) {
		uint id_next = (idx + 2 < ubo.count) ? idx + 2: idx;
		a = p0;
		p = p1;
		b = vec4(data[id_next].pos,0,1);

		uv.x = 1;
		s = s1;
	}

	vec2 final;
	bool is_mid = bool(id & MID_BIT); 

	if (is_mid) {
	  	final = 0.5*(p.xy + a.xy) + Y;
		uv.x = 0.5;
		s = s0 + 0.5*ds;
	} else {
	  	final = intersect_join(a.xy,p.xy,b.xy,Y);

		float factor = len/ubo.thickness; 
		vec2 r = final.xy - p.xy;
		float extra = dot(r,X_abs);

		uv.x += extra/len;
	}

	vec4 pos = vec4(final.x,final.y,p.z,p.w);

	gl_Position = u_frame.pv * pos;

	frag_pos = pos;
	dir = X;
	center = p.xy;

	out_uv = vec2(uv.x*(0.5*len/ubo.thickness),uv.y);
	out_total_length = s;

	instance_id = gl_InstanceIndex;
	out_length = len;

	// This is so the provoking vertex passes the 
	// correct values for flat shader outputs
	if (is_mid && bool(id & VERT_UPPER)) {
		center = p1.xy;
		dir = -X;
	}

}

