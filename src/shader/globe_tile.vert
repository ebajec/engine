#version 450 core

#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"
#include "globe.glsl"

//------------------------------------------------------------------------------
// Vert

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 uv;
layout (location = 2) in vec3 normal;
layout (location = 3) in uint code_left;
layout (location = 4) in uint code_right;

layout (location = 0) out vec3 out_pos;
layout (location = 1) out vec2 out_uv;
layout (location = 2) out vec3 out_normal;
layout (location = 3) out vec4 out_color;
layout (location = 4) flat out tile_code_t out_code;
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

void main()
{
	float t = u_frame.t;
	mat4 pv = u_frame.pv;

	vec4 wpos = vec4(pos, 1);
	vec4 n = vec4(normal,0);

	uint tile_idx = gl_VertexID/TILE_VERT_COUNT;
	tex_idx_t tex_idx = decode_tex_idx(tex_indices[tile_idx]);

	vec3 uvw = vec3(uv, tex_idx.tex);
	vec4 val = texture(u_tex_arrays[tex_idx.page], uvw);

	//tile_code_t code = from_input(code_left,code_right);
	//aabb2_t rect = morton_u64_to_rect_f64(code.idx,code.zoom);
	//vec2 drect = rect.max - rect.min;
	//vec4 c = texture(u_tex,rect.min + uv*drect);
	float f = 15;
	float d = sin(f*wpos.x - 2*t)*cos(f*wpos.y - 2*t)*
			sin(f*wpos.z + 2*t)*cos(f*wpos.z + 2*t);

	wpos += 0.1*n*(length(val.xyz));
	//wpos += 0.1*n*(length(val) - 0.5);
	//wpos += n*0.1*d;

	//tex_idx.tex = tile_idx;

	out_pos = wpos.xyz;
	out_uv = uv;
	out_normal = n.xyz;
	out_color = val;
	//out_code = code;
	out_tex_idx = tex_idx;

	gl_Position = (pv*wpos);
}

