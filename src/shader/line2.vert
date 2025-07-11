#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"

struct vert_data_t
{
	vec2 pos;
};

layout (location = 0) in uint id;

layout (std430, binding = 0) buffer Vertices
{
	vert_data_t data[];
};

layout (location = 0) out vec4 frag_color;
layout (location = 1) out vec4 frag_pos;
layout (location = 2) out vec2 out_x;
layout (location = 3) out vec2 out_y;

const uint SIDE_LEFT = 0;
const uint SIDE_RIGHT = 0x1;

const uint VERT_LOWER = 0x0;
const uint VERT_UPPER = 0x2;

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

const uint max_idx = 10000000;
const float thickness = 0.1; 

void main()
{
	uint idx = gl_InstanceIndex; 

	vert_data_t v[2] = {data[idx],data[idx + 1]};

	vec4 p0 = u_frame.pv*vec4(v[0].pos,0,1);
	vec4 p1 = u_frame.pv*vec4(v[1].pos,0,1);

	vec2 X = normalize(p1.xy - p0.xy);
	vec2 Y = vec2(X.y, -X.x);

	uint side = id & 0x1;

	vec2 offset = vec2(0);
	vec4 p = vec4(0);

	vec4 a = vec4(0), b = vec4(0);
	if (side == SIDE_LEFT) {
		uint id_prev = (idx > 0) ? idx - 1 : 0;
		a = p1;
		p = p0;
		b = u_frame.pv*vec4(data[id_prev].pos,0,1);

	} 
	else if (side == SIDE_RIGHT) {
		uint id_next = (idx > max_idx) ? idx : idx + 2;
		a = p0;
		p = p1;
		b = u_frame.pv*vec4(data[id_next].pos,0,1);
	}

	uint vert = id & 0x2;
	if (vert == VERT_LOWER) {
		Y = -Y;	
	} else if (vert == VERT_UPPER) {
		Y = Y;
	}
	
	Y *= thickness;

	vec2 final = intersect_join(a.xy,p.xy,b.xy,Y);
	vec4 pos = vec4(final.x,final.y,p.z,p.w);

	gl_Position = pos;

	vec4 color = vec4(1,0,0,0.5);

	frag_pos = pos;
	frag_color = color;
	out_x = X;
	out_y = Y;
}

