#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"

//----------------------------------------------------------------------------------------
// Frag

layout (binding = 0) uniform sampler2D u_tex;

layout (location = 0) in vec3 frag_pos;
layout (location = 1) in vec2 frag_uv;
layout (location = 2) in vec3 frag_normal;

layout (location = 0) out vec4 FragColor;

vec4 diff(vec2 uv)
{
	float h = 0.005;
	vec4 sx0 = texture(u_tex,uv - vec2(h,0));
	vec4 sy0 = texture(u_tex,uv - vec2(0,h));
	vec4 sx1 = texture(u_tex,uv + vec2(h,0));
	vec4 sy1 = texture(u_tex,uv + vec2(0,h));

	vec4 ddx = (sx1 - sx0)/(2.0*h);
	vec4 ddy = (sy1 - sy0)/(2.0*h);

	return ddy;
}

vec2 polar(vec2 p) 
{
	float theta = p.x*TWOPI;
	float r = p.y;

	vec2 uv = r*vec2(cos(theta),sin(theta));
	uv = 0.5*(uv + vec2(1.0));
	return uv;
}

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
	float t = ftime();

	vec2 uv = frag_uv;
	vec4 c0 = texture(u_tex,uv);

	vec3 sun = normalize(vec3(0.2,0.2,1));
	vec3 n = frag_normal;

	float f = 0.2 + 0.8*clamp(dot(n,sun),0,1);

	vec2 vel = c0.xy;

	float intensity = abs(2*c0.z); 

	vec3 c = palette(tanh(intensity));

	FragColor = vec4(f*c,1); 
}
