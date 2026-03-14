#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"

layout (location = 0) out vec3 out_pos;
layout (location = 1) out vec4 out_color;

layout (binding = 0) uniform sampler2D u_tex;

vec3 palette(float t)
{
	t = tanh(t);
	return mix(vec3(1,0,0), vec3(0,1,1), t);
}

void main()
{
	int base = gl_InstanceIndex;
	bool parity = bool(gl_VertexIndex & 0x1);

	ivec2 size = 2*textureSize(u_tex, 0);

	int px = base / size.x;
	int py = base - size.y * px;

	vec2 uv = (vec2(px, py) + vec2(0.5))/vec2(size);

	vec4 value = texture(u_tex, uv);

	vec2 F = value.xy;
	float norm = length(F);

	F = norm > 1e-4 ? F : vec2(0);

	float scale = 4./size.x;

	vec2 c = 2*uv - vec2(1);

	float D = 2;

	float t = D*norm;

	vec2 pos = parity ? c : c + D*F*scale; 
	vec4 color = parity ? vec4(0) : vec4(palette(t),1);

	out_pos = vec3(pos, 0);
	out_color = color;

	vec4 final = u_view.pv * vec4(pos, 0, 1);
	
	gl_Position = vec4(final.xyz,1.0);
}
