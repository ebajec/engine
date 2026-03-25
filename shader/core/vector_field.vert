#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"

layout (location = 0) out vec3 out_pos;
layout (location = 1) out vec4 out_color;

layout (binding = 0) uniform sampler2D u_tex;

vec3 palette(float t) {
	t = clamp(t,0,1);
    // Vibrant Jet-style palette
    // t: 0.0 (blue/violet) → 0.5 (green/yellow) → 1.0 (red/magenta)
    vec3 col = vec3(0.0);

    // Red channel
    col.r = clamp(1.5 - abs(4.0 * t - 3.0), 0.0, 1.0);

    // Green channel
    col.g = clamp(1.5 - abs(4.0 * t - 2.0), 0.0, 1.0);

    // Blue channel
    col.b = clamp(1.5 - abs(4.0 * t - 1.0), 0.0, 1.0);

    // Boost saturation — push away from grey
    float lum = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(lum), col, 1.35);

    return clamp(col, 0.0, 1.0);
}
void main()
{
	int base = gl_InstanceIndex;
	bool parity = bool(gl_VertexIndex & 0x1);

	ivec2 size = textureSize(u_tex, 0);

	int px = base / size.x;
	int py = base - size.y * px;

	vec2 uv = (vec2(px, py) + vec2(0.5))/vec2(size);

	vec4 value = texture(u_tex, uv);

	vec2 F = value.xy;
	float norm = length(F);

	F = norm > 1e-4 ? F : vec2(0);

	float scale = 4./size.x;

	vec2 c = 2*uv - vec2(1);

	float t = norm;

	vec2 pos = parity ? c : c + F*scale; 
	vec4 color = parity ? vec4(0) : vec4(palette(norm),0.6);

	out_pos = vec3(pos, 0);
	out_color = color;

	vec4 final = u_view.pv * vec4(pos, 0, 1);
	
	gl_Position = vec4(final.xyz,1.0);
}
