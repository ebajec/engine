#version 430 core
#extension GL_GOOGLE_include_directive : require

//--------------------------------------------------------------------------------------------------
// Frag

layout (binding = 0) uniform sampler2D u_tex;

layout (binding = 1) uniform UBO
{
	float u_s;
};

layout (location = 0) in vec2 frag_pos;
layout (location = 1) in vec2 frag_uv;

layout (location = 0) out vec4 FragColor;

struct tgrad_t
{
	vec4 du;
	vec4 dv;
};

tgrad_t tex_grad(vec2 uv, float h)
{
	float u1 = max(uv.x - h,h);
	float u2 = min(uv.x + h,1.0 - h);

	vec4 fu1 = texture(u_tex, vec2(u1,uv.y));
	vec4 fu2 = texture(u_tex, vec2(u2,uv.y));

	vec4 dfdu = (fu2 - fu1)/(u2 - u1);

	float v1 = max(uv.y - h,h);
	float v2 = min(uv.y + h,1.0 - h);

	vec4 fv1 = texture(u_tex, vec2(uv.x, v1));
	vec4 fv2 = texture(u_tex, vec2(uv.x, v2));

	vec4 dfdv = (fv2 - fv1)/(v2 - v1);

	return tgrad_t(dfdu,dfdv);
}

tgrad_t tex_grad2(vec2 uv)
{
	ivec2 size = textureSize(u_tex, 0);

	vec2 h = 0.5/vec2(size);

	uv = floor(vec2(size)*uv)/vec2(size);

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

	return tgrad_t(dfdu,dfdv);
}

vec2 adjust_uv_for_clamp(vec2 uv, float h)
{
	return vec2(h) + uv*(1-2.0*h); 
}

void main()
{
	ivec2 size = textureSize(u_tex, 0);

	vec2 uv = frag_uv;
	float h = 1.0/(2.0*size.x);
	float h2 = 1.0/(size.x);

	tgrad_t grad = tex_grad2(uv);

	ivec2 texel = ivec2(adjust_uv_for_clamp(uv,h)*vec2(size));

	vec4 samp[4] = {
		texelFetch(u_tex,texel + ivec2(1,0),0),
		texelFetch(u_tex,texel + ivec2(-1,0),0),
		texelFetch(u_tex,texel + ivec2(0,1),0),
		texelFetch(u_tex,texel + ivec2(0,-1),0)
		//texture(u_tex,uv + vec2(h,0)),
		//texture(u_tex,uv + vec2(-h,0)),
		//texture(u_tex,uv + vec2(0,h)),
		//texture(u_tex,uv + vec2(0,-h))
	};

	vec4 avg = 1.0/4.0 * (samp[0] + samp[1] + samp[2] + samp[3]);

	//vec4 color = texelFetch(u_tex,texel,0);
	vec4 color = texture(u_tex,uv);

	float ddu = dot(grad.du,grad.du);
	float ddv = dot(grad.dv,grad.dv);

	vec2 diff = vec2(
		dot(color/length(color),grad.du),
		dot(color/length(color),grad.dv)
	);

	float f = 0.01*length(diff);

	//f = length(color.xyz)/sqrt(3);

	FragColor = mix(color,f*vec4(1),u_s);
}

