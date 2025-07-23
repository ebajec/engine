#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"
#include "line.glsl"

layout (location = 0) in vec2 in_pos;
layout (location = 1) in vec2 in_uv;
layout (location = 5) in float in_total_length;

layout (location = 12) flat in vec2 in_X;
layout (location = 10) flat in vec2 in_p0;
layout (location = 11) flat in vec2 in_p1;
layout (location = 14) flat in vec2 in_deltas;
layout (location = 16) flat in vec2 in_join_angles;
layout (location = 17) flat in vec2 in_s_start;

layout (location = 0) out vec4 FragColor;

bool clip_round_join(vec2 d, float thickness)
{
	if (d.x > 0 && length(d) > ubo.thickness) {
		discard;
		return true;
	}
	return false;
}

struct JoinInfo
{
	float delta;
	float angle;
	float s_start;
	float width;
	float sgnX;
	float delta_sgn;
	vec2 center;
	mat2 frame;
};

JoinInfo create_join()
{
	JoinInfo join;

	bool left = in_uv[0] < 0.5;
	uint side = left ? 0 : 1;

	join.sgnX = left ? -1 : 1;

	vec2 dir = join.sgnX*in_X;

	join.center = left ? in_p0 : in_p1;  
	join.delta = in_deltas[side];
	join.delta_sgn = join.delta < 0 ? -1 : 1;
	join.frame = mat2(dir,join.delta_sgn*vec2(dir.y,-dir.x));

	join.width = ubo.thickness;
	join.angle = in_join_angles[side];
	join.s_start = in_s_start[side];

	return join;
}
const bool round = true;

vec2 smooth_corner_uv(vec2 p, vec2 d, JoinInfo join)
{

	vec2 uv = in_uv;
	float s;

	float delta_abs = abs(join.delta);
	float width = join.width;

	if (abs(d.x) < delta_abs && !limit_join(delta_abs)) {
		vec2 o = join.center - join.frame*vec2(delta_abs,width); 

		vec2 diff = in_pos - o;
		float r = length(diff);
		diff /= r;

		float prod = dot(diff,join.frame[1]);

		float tht1 = acos(clamp(prod,-1,1)); 
		float tht2 = join.angle; 

		float t = tht1/tht2;

		if (round) {
			float tht_circ = atan(delta_abs,(2.0*width)); 
			if (tht1 > tht_circ) {
				float delta_sq = delta_abs*delta_abs;
				float w_sq = width*width;

				float sum = delta_sq + w_sq;
				float h = sqrt(sum);

				float tht = tht2 - tht1;
				float cos_tht = cos(tht);

				float r_circ = h*cos_tht + sqrt(sum*cos_tht*cos_tht - delta_sq);
				float uv_y_norm = r/r_circ;

				bool flip_uv_y = join.delta_sgn * join.sgnX > 0; 
				uv.y = flip_uv_y ? 1 - uv_y_norm : uv_y_norm; 
			}
		}

		s = join.s_start + join.sgnX*t*delta_abs;
		uv.x = s * (0.5/width);
	} else {
		s = in_total_length;
		uv.x = s * (0.5/width);
	}

	return uv;
}

//#define OLD

void main()
{
#ifdef OLD
	bool left = in_uv[0] < 0.5;
	float sgnX = left ? -1 : 1;
	vec2 center = left ? in_p0 : in_p1;  

	vec2 dir = sgnX*in_X;

	float width = ubo.thickness;

	uint side = left ? 0 : 1;

	float delta = in_deltas[side];

	// doing sign(delta) will give zero if delta is zero. 
	// we do not want that
	float delta_sgn = delta < 0 ? -1 : 1;
	float delta_abs = abs(delta);

	mat2 m = mat2(dir,delta_sgn*vec2(dir.y,-dir.x));

	vec2 p = in_pos - center;
	vec2 d = transpose(m)*p;

	if (round)
		clip_round_join(d,width);
#else
	JoinInfo join = create_join();

	vec2 p = in_pos - join.center;
	vec2 d = transpose(join.frame)*p;

	if (round)
		clip_round_join(d,join.width);

	vec2 uv = smooth_corner_uv(p,d,join);
#endif

#ifdef OLD
	vec2 uv = in_uv;
	float s;

	if (abs(d.x) < delta_abs && !limit_join(delta)) {
		float join_angle = in_join_angles[side];
		float s_start = in_s_start[side];

		vec2 o = center - m*vec2(delta_abs,width); 

		vec2 diff = in_pos - o;
		float r = length(diff);
		diff /= r;

		float prod = dot(diff,m[1]);

		float tht1 = acos(clamp(prod,-1,1)); 
		float tht2 = join_angle; 

		float t = tht1/tht2;

		if (round) {
			float tht_circ = atan(delta_abs,(2.0*width)); 
			if (tht1 > tht_circ) {
				float delta_sq = delta_abs*delta_abs;
				float w_sq = width*width;

				float sum = delta_sq + w_sq;
				float h = sqrt(sum);

				float tht = tht2 - tht1;
				float cos_tht = cos(tht);

				float r_circ = h*cos_tht + sqrt(sum*cos_tht*cos_tht - delta_sq);
				float uv_y_norm = r/r_circ;

				bool flip_uv_y = delta_sgn * sgnX > 0; 
				uv.y = flip_uv_y ? 1 - uv_y_norm : uv_y_norm; 

			}
		}

		s = s_start + sgnX*t*delta_abs;
		uv.x = s * (0.5/width);
	} else {
		s = in_total_length;
		uv.x = s * (0.5/width);
	}
#endif

 	uv.x -= u_frame.t;

	float fx = d.x > 0 ? length(p) : abs(d.y);
	fx /= 2*ubo.thickness;
	fx = mod(uv.x,4) < 2 ? 1 : 0;
	FragColor = texture(u_tex,uv);
	FragColor += vec4(fx,0,0,fx);
}
