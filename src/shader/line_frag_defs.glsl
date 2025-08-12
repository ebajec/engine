#ifndef LINE_FRAG_DEFS_GLSL
#define LINE_FRAG_DEFS_GLSL

#include "line.glsl"

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec2 in_uv;
layout (location = 5) in float in_total_length;
layout (location = 2) in float in_width;

layout (location = 4) flat in float in_mid_weight;
layout (location = 12) flat in vec2 in_X;
layout (location = 10) flat in vec2 in_p0;
layout (location = 11) flat in vec2 in_p1;
layout (location = 17) flat in vec2 in_s;
layout (location = 14) flat in vec2 in_deltas;
layout (location = 16) flat in vec2 in_join_angles;
layout (location = 18) flat in vec2 in_join_dir[2];
layout (location = 20) flat in uint dbg_draw;
layout (location = 21) flat in vec2 in_hypot;
layout (location = 22) flat in uint in_endpoint;
layout (location = 23) flat in vec2 in_bevel;

layout (location = 0) out vec4 FragColor;

const uint JOIN_TYPE_ROUND = 0;
const uint JOIN_TYPE_BEVEL = 1;
const uint JOIN_TYPE_MITRE = 2;

struct JoinInfo
{
	float delta;
	float angle;
	float s_start;
	float width;
	float sgnX;
	float delta_sgn;
	float hypot;
	float bevel;
	vec2 center;
	vec2 B;
	mat2 frame;
};

JoinInfo create_join()
{
	JoinInfo join;

	bool left = in_uv[0] < in_mid_weight;
	uint side = left ? 0 : 1;

	join.sgnX = left ? -1 : 1;

	vec2 dir = join.sgnX*in_X;

	join.center = left ? in_p0 : in_p1;
	join.delta = in_deltas[side];
	join.delta_sgn = join.delta < 0 ? -1 : 1;
	join.frame = mat2(dir,join.delta_sgn*vec2(dir.y,-dir.x));

	join.width = in_width;
	join.angle = in_join_angles[side];
	join.s_start = left ?
		in_s[0] + abs(join.delta) :
		in_s[1] - abs(join.delta);
	join.B = join.delta_sgn*in_join_dir[side];
	join.hypot = in_hypot[side];
	join.bevel = in_bevel[side];

	return join;
}

bool clip_join(vec2 p, vec2 d, JoinInfo join, uint type)
{
	bool clip = false;

	float w_sq = join.width * join.width;

	switch (type) {
	case JOIN_TYPE_ROUND:
		return d.x > 0 && dot(d,d) > w_sq;
	case JOIN_TYPE_BEVEL: {
		float prod = dot(p,join.B); 
		return prod > 0 && (prod > join.bevel);
	}
	}

	return false;
}

vec2 compute_corner_uv(vec2 p, vec2 d, JoinInfo join, uint type)
{
	vec2 uv = in_uv;
	float s;

	float delta_abs = abs(join.delta);

	float width = join.width;

	if (abs(d.x) > delta_abs || limit_join(delta_abs,width) || 
		(bool(in_endpoint & RIGHT_ENDPOINT) && join.sgnX > 0) ||
		(bool(in_endpoint & LEFT_ENDPOINT) && join.sgnX < 0)
	) {
		s = in_total_length;
		uv.x = s * (0.5/width);
		return uv;
	}

	vec2 o = join.center - join.frame*vec2(delta_abs,width);

	vec2 diff = in_pos - o;
	vec2 w = transpose(join.frame)*(diff);

	float tht1 = atan(w.x,w.y);
	float tht2 = join.angle;

	float t = tht1/tht2;

	bool flip_uv_y = join.delta_sgn * join.sgnX > 0;

	if (type == JOIN_TYPE_ROUND) {
		float r = length(w);
		float tht_circ = atan(delta_abs,(2.0*width));
		if (tht1 > tht_circ) {
			float delta_sq = delta_abs*delta_abs;
			float w_sq = width*width;

			float sum = delta_sq + w_sq;
			float h = join.hypot;

			float tht = tht2 - tht1;
			float cos_tht = dot(diff/r,join.B);

			float r_circ = h*cos_tht + sqrt(sum*cos_tht*cos_tht - delta_sq);
			float uv_y_norm = r/r_circ;

			uv.y = flip_uv_y ? 1 - uv_y_norm : uv_y_norm;
		}
	}
	else if (type == JOIN_TYPE_BEVEL) {
		float r = length(w);
		float tht_circ = atan(delta_abs,(2.0*width));
		if (tht1 > tht_circ) {
			float tht = tht2 - tht1;
			float cos_tht = dot(diff/r,join.B);

			float h = join.hypot + join.bevel;

			float uv_y_norm = r*cos_tht/h;

			uv.y = flip_uv_y ? 1 - uv_y_norm : uv_y_norm;
		}
	}

	s = join.s_start + join.sgnX*t*delta_abs;
	uv.x = s * (0.5/width);

	return uv;
}

#endif //LINE_FRAG_DEFS_GLSL

