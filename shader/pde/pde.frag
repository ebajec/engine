#version 430 core
#extension GL_GOOGLE_include_directive : require

#include "../core/frame.glsl"
#include "pde.glsl"

//--------------------------------------------------------------------------------------------------
// Frag

layout (set = PER_DRAW_SET, binding = 0) uniform sampler2D u_tex;

layout (location = 0) in vec2 frag_pos;
layout (location = 1) in vec2 frag_uv;

layout (location = 0) out vec4 FragColor;

void tex_grad2(vec2 uv, out vec4 du, out vec4 dv)
{
	ivec2 size = textureSize(u_tex, 0);

	vec2 h = 1/vec2(size);

	float u1 = max(uv.x - h.x,h.x);
	float u2 = min(uv.x + h.x,1.0 - h.x);

	vec4 fu1 = texture(u_tex, vec2(u1,uv.y));
	vec4 fu2 = texture(u_tex, vec2(u2,uv.y));

	vec4 dfdu = (fu2 - fu1)/(u2 - u1);

	float v1 = max(uv.y - h.y,h.y);
	float v2 = min(uv.y + h.y,1.0 - h.y);

	vec4 fv1 = texture(u_tex, vec2(uv.x, v1));
	vec4 fv2 = texture(u_tex, vec2(uv.x, v2));

	vec4 dfdv = (fv2 - fv1)/(v2 - v1);

	float scale = 10/sqrt(size.x*size.y);

	du = scale*dfdu;
	dv = scale*dfdv;
}

void main()
{
	ivec2 size = textureSize(u_tex, 0);

	vec4 grad_u, grad_v;
	vec2 uv = frag_uv;
	tex_grad2(uv, grad_u, grad_v);

	ivec2 texel = ivec2(uv*vec2(size) + 0.5);

	vec4 samp[4] = {
		texelFetch(u_tex,texel + ivec2(0,0),0),
		texelFetch(u_tex,texel + ivec2(1,0),0),
		texelFetch(u_tex,texel + ivec2(0,0),0),
		texelFetch(u_tex,texel + ivec2(0,1),0)
	};

	vec4 avg = 1.0/4.0 * (samp[0] + samp[1] + samp[2] + samp[3]);

	vec4 val = texture(u_tex,uv);

	vec2 grad_x = vec2(grad_u.w,grad_v.w);

	vec3 sun = normalize(vec3(0.5,0.2,-0.5));
	vec3 n = normalize(vec3(grad_x.x,grad_x.y,0.1));

	float f = 0.5 + 0.5*clamp(dot(n,sun),0,1);

	float speed = 0.5*length(val.xy);

	float color_val = 2*abs(val.w);

	vec4 color = vec4(color_val,0.2*speed, speed, color_val);

	//color = vec4(0);
	color *= 0.6;
	color.r = speed*color.b;

	ivec2 pix = ivec2(uv*vec2(size));

	if (uv.x < 0.f || uv.y < 0.f || uv.x > 1.f || uv.y > 1.f) {
		FragColor = vec4(0.5);
	} else {
		FragColor = vec4(f*color.rgb,color.a);
	}
}
