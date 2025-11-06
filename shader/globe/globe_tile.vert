#version 450 core

#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"

#include "globe.glsl"

//------------------------------------------------------------------------------
// Vert

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 in_uv;
layout (location = 2) in vec3 normal;

layout (location = 0) out vec3 out_pos;
layout (location = 1) out vec2 out_uv;
layout (location = 2) out vec3 out_normal;
layout (location = 3) out vec4 out_color;
layout (location = 7) flat out tex_idx_t out_tex_idx;

const float GLOBE_SCALE = 0.00138f;

aabb2_t morton_u32_to_rect_f32(uint index, uint level)
{
    aabb2_t extent;
	extent.min = vec2(0);
	extent.max = vec2(0);

	float h = 1.0/float(1 << level);

	for (; level > 0; --level) {
        uint xi = index & 0x1;
        index >>= 1;
        uint yi = index & 0x1;
        index >>= 1;

		float hi = 1.0/float(1 << level);

        extent.min.x += bool(xi) ? hi : 0.0;
        extent.min.y += bool(yi) ? hi : 0.0;
	}

	extent.max.x = extent.min.x + h;
	extent.max.y = extent.min.y + h;

    return extent;
}

tile_code_t from_input(uint left, uint right)
{
	tile_code_t code;
	code.face = (left & 0x7u); 
	code.zoom = (left >> 3) & 0x1Fu;
	code.idx = (left >> 8);

	return code;
}

vec2 adjust_uv_for_clamp(vec2 uv)
{
	return vec2(1.0/512.0) + uv*(1-1.0/256.0); 
}

vec3 face_to_world(vec3 v, uint face)
{
	switch (face)
	{
	case 0:
		return vec3(v.z,v.x,v.y);
	case 1:
		return vec3(-v.x,v.z,v.y);
	case 2:
		return vec3(-v.y,v.x,v.z);
	case 3:
		return vec3(-v.z,v.x,-v.y);
	case 4:
		return vec3(v.x,-v.z,v.y);
	case 5:
		return vec3(v.y,v.x,-v.z);
	} 
	return vec3(0);
}

vec3 cube_to_globe(uint face, vec2 uv) 
{
	vec3 c = vec3((2.0*uv - vec2(1.0)),1);
	return normalize(face_to_world(c, face));
}

float sample_tex(tex_idx_t idx, vec2 uv)
{
	float h = texture(u_tex_arrays[idx.page], vec3(uv, idx.tex)).r; 

//	tile_code_t code = from_input(code_left, code_right);
//	aabb2_t rect = morton_u32_to_rect_f32(code.idx, code.zoom);
//
//	vec3 p = cube_to_globe(code.face, mix(rect.min, rect.max, uv));
//
//
//	float t = TWOPI*fract(u_frame.t);
//	float h1 = 1;
//	float h2 = 1;
//
//	float f1 = 3;
//	float f2 = 40;
//
//	float p2 = 1;
//
//	float r = sin(f1*p.x - h1*t)*cos(sqrt(2)*f1*p.y - h1*t)*sin(3*f1*p.z - h1*t);
//
//	r += 0.1*sin(f2*p.x - h2*t - p2)*cos(sqrt(2)*f2*p.y - h2*t - p2)*sin(3*f2*p.z - h2*t - p2);
//
	//h += 0.1*r;
	return h;
}

vec2 tex_grad(tex_idx_t idx, vec2 uv, uint zoom)
{
	uint pg = idx.page;
	uint tx = idx.tex;

	float h = 1.0/512.0;
	float s = 1 << zoom;

	float u1 = max(uv.x - h,h);
	float u2 = min(uv.x + h,1.0 - h);

	float fu1 = sample_tex(idx, vec2(u1,uv.y));
	float fu2 = sample_tex(idx, vec2(u2,uv.y));

	float dfdu = (fu2 - fu1)/(u2 - u1);

	float v1 = max(uv.y - h,h);
	float v2 = min(uv.y + h,1.0 - h);

	float fv1 = sample_tex(idx, vec2(uv.x, v1));
	float fv2 = sample_tex(idx, vec2(uv.x, v2));

	float dfdv = (fv2 - fv1)/(v2 - v1);

	return vec2(dfdu,dfdv)*s;
}

vec3 globe_normal(vec3 n, float f, vec2 df, vec2 uv, uint face, uint zoom)
{
	float s = 1 << zoom;
	float u = uv.x;
	float v = uv.y;
	float frac = pow(1 + u*u + v*v,-3.0/2.0);
	vec3 Su = vec3(1 + v*v, -u*v, -u)*frac;
	vec3 Sv = vec3(-u*v, 1 + u*u, -v)*frac;

	vec3 Mu = face_to_world(Su*(1.0 + f),face) + n*df.x;
	vec3 Mv = face_to_world(Sv*(1.0 + f),face) + n*df.y;

	return normalize(cross(Mu,Mv));
}

vec4 palette(float v)
{
	return vec4(hsv2rgb(vec3(v + 0.57,1-0.2*(v + 1),1-0.2*(v + 1))),1);
}

void main()
{
	mat4 pv = u_frame.pv;

	vec4 wpos = vec4(pos, 1);

	uint tile_idx = gl_VertexID/TILE_VERT_COUNT;

	metadata_t mdata = metadata[tile_idx];

	tex_idx_t tex_idx = decode_tex_idx(mdata.tex_idx);
	tile_code_t code = from_input(mdata.code_lower,mdata.code_upper);

	vec2 tex_uv = mix(mdata.tex_uv[0], mdata.tex_uv[1], in_uv);
	vec2 face_uv = mix(mdata.globe_uv[0], mdata.globe_uv[1], in_uv);

	vec2 uv = adjust_uv_for_clamp(tex_uv); 
	vec3 n = normal;

	vec3 N = vec3(0);
	vec4 val = vec4(0);
	vec2 df = vec2(0);

	float f = 0;
	if (is_valid(tex_idx)) {
		f = sample_tex(tex_idx, uv);
		df = tex_grad(tex_idx, uv, code.zoom);

		N = globe_normal(n,f,df,2.0*face_uv - vec2(1.0),code.face,code.zoom);

		wpos += vec4(n*f,0);
	}

	float d = 100*f;

	out_pos = wpos.xyz;
	out_uv = uv;
	out_normal = N;
	out_color = palette(d); 
	out_tex_idx = tex_idx;

	gl_Position = (pv*wpos);
}

