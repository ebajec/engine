#version 430 core
#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"

layout (location = 0) out vec3 out_pos;
layout (location = 1) out vec2 out_uv;
layout (location = 2) out vec3 out_normal;

layout (binding = 0) uniform sampler2D u_tex;

struct tgrad_t
{
	vec4 du;
	vec4 dv;
};

tgrad_t tex_grad2(vec2 uv)
{
	ivec2 size = textureSize(u_tex, 0);

	vec2 h = 0.5/vec2(size);

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

void main()
{
	ivec2 size = textureSize(u_tex, 0);

	int tx = gl_VertexIndex/size.x;
	int ty = gl_VertexIndex - size.y * tx;

	vec2 uv = vec2(tx, ty)/vec2(size);

	float scale = 0.2f;
	vec4 tex = texture(u_tex, uv)*scale; 

	tgrad_t grad = tex_grad2(uv);
	grad.du *= scale;
	grad.dv *= scale;

	float z = tex.x;
	vec3 n = normalize(vec3(grad.du.x, grad.dv.x, 1));

	out_pos = vec3(uv,z);
	out_uv = uv;
	out_normal = n;

	gl_Position = u_view.pv * vec4(uv,z,1);
}
