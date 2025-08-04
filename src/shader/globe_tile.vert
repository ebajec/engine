#version 450 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"

struct tile_code_t
{
	uint face;
	uint zoom; 
	uint idx;
};

struct aabb2_t
{
	vec2 min, max;
};

//------------------------------------------------------------------------------
// Vert

layout (binding = 0) uniform sampler2D u_tex;

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 uv;
layout (location = 2) in vec3 normal;
layout (location = 3) in uint code_left;
layout (location = 4) in uint code_right;

layout (location = 0) out vec3 frag_pos;
layout (location = 1) out vec2 frag_uv;
layout (location = 2) out vec3 frag_normal;
layout (location = 3) out vec4 fcolor;
layout (location = 4) flat out tile_code_t out_code;

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

	tile_code_t code = from_input(code_left,code_right);

	aabb2_t rect = morton_u64_to_rect_f64(code.idx,code.zoom);

	vec2 drect = rect.max - rect.min;

	vec4 c = texture(u_tex,rect.min + uv*drect);

	float f = 15;

	float d = sin(f*wpos.x - 2*t)*cos(f*wpos.y - 2*t)*
			sin(f*wpos.z + 2*t)*cos(f*wpos.z + 2*t);

	//wpos += n*0.05*length(c);

	//wpos += n*0.1*d;

	frag_pos = wpos.xyz;
	frag_uv = uv;
	frag_normal = n.xyz;
	fcolor = c;
	out_code = code;

	gl_Position = (pv*wpos);
}

