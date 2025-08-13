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
layout (location = 3) in uint code_left;
layout (location = 4) in uint code_right;

layout (location = 0) out vec3 out_pos;
layout (location = 1) out vec2 out_uv;
layout (location = 2) out vec3 out_normal;
layout (location = 3) out vec4 out_color;
layout (location = 7) flat out tex_idx_t out_tex_idx;

uint TILE_CODE_FACE_BITS_MASK = 0x70000000;
uint TILE_CODE_FACE_BITS_SHIFT = 0;

uint TILE_CODE_ZOOM_BITS_MASK = 0x8F000000;
uint TILE_CODE_ZOOM_BITS_SHIFT = 3;

uint TILE_CODE_IDX_BITS_MASK = 
	~(TILE_CODE_FACE_BITS_MASK | TILE_CODE_ZOOM_BITS_MASK);

aabb2_t morton_u64_to_rect_f64(uint index, uint level)
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

vec3 surface_normal(tex_idx_t idx, vec2 uv, uint zoom)
{
	uint pg = idx.page;
	uint tx = idx.tex;

	float h = 1.0/512.0;

	float u1 = clamp(uv.x - h,h,1 - h);
	float u2 = clamp(uv.x + h,h,1 - h);

	float fu1 = texture(u_tex_arrays[pg], vec3(vec2(u1,uv.y), tx)).r;
	float fu2 = texture(u_tex_arrays[pg], vec3(vec2(u2,uv.y), tx)).r;

	float dfdu = (fu2 - fu1)/(u2 - u1);

	float v1 = clamp(uv.y - h,h,1 - h);
	float v2 = clamp(uv.y + h,h,1 - h);

	float fv1 = texture(u_tex_arrays[pg], vec3(vec2(uv.x,v1), tx)).r;
	float fv2 = texture(u_tex_arrays[pg], vec3(vec2(uv.x,v2), tx)).r;

	float dfdv = (fv2 - fv1)/(v2 - v1);

	vec3 N = normalize(vec3(-dfdu,-dfdv,1.0/(1<<zoom)));

	return N;
}

vec3 face_to_world(vec3 v, uint face)
{
	switch (face)
	//{
	//case 0:
	//	return vec3(v.y,v.z,v.x);
	//case 1:
	//	return vec3(-v.x,v.z,v.y);
	//case 2:
	//	return vec3(v.y,-v.x,v.z);
	//case 3:
	//	return vec3(v.y,-v.z,-v.x);
	//case 4:
	//	return vec3(v.x,v.z,-v.y);
	//case 5:
	//	return vec3(v.y,v.x,-v.z);
	//} 
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
	//return vec3(0);
}

void main()
{
	float t = u_frame.t;
	mat4 pv = u_frame.pv;

	vec4 wpos = vec4(pos, 1);
	vec4 n = vec4(normal,0);

	uint tile_idx = gl_VertexID/TILE_VERT_COUNT;
	tex_idx_t tex_idx = decode_tex_idx(tex_indices[tile_idx]);

	tile_code_t code = from_input(code_left,code_right);

	vec2 uv = adjust_uv_for_clamp(in_uv); 
	vec3 uvw = vec3(uv, tex_idx.tex);
	bool valid = is_valid(tex_idx); 

	vec3 N = vec3(0);
	vec4 val = vec4(0);

	if (valid) {
		val = texture(u_tex_arrays[tex_idx.page], uvw);
		N = surface_normal(tex_idx,uv,code.zoom);
		N = face_to_world(N,code.face);

		vec3 axis = cross(face_to_world(vec3(0,0,1),code.face),normal);
		float len = length(axis);
		if (len > 1e-6) {
			float tht = asin(len);
			vec4 q = qexp(axis/len,1.*tht);
			vec4 qr = qmult(qmult(q,vec4(N,0)),qconj(q));
			N = qr.xyz;
		}
		

		wpos += n*(val.r);
	}

	out_pos = wpos.xyz;
	out_uv = uv;
	out_normal = N;
	out_color = val;
	out_tex_idx = tex_idx;

	gl_Position = (pv*wpos);
}

