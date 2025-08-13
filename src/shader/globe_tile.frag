#version 450 core

#extension GL_GOOGLE_include_directive : require
#include "framedata.glsl"
#include "common.glsl"
#include "globe.glsl"

//------------------------------------------------------------------------------
// Frag

layout (location = 0) in vec3 in_pos;
layout (location = 1) in vec2 in_uv;
layout (location = 2) in vec3 in_normal;
layout (location = 3) in vec4 in_color;
layout (location = 7) flat in tex_idx_t in_tex_idx;

layout (location = 0) out vec4 FragColor;

const vec4 FACE_COLORS[6] = {
	vec4(1,0,0,1),
	vec4(0,1,0,1),
	vec4(0,0,1,1),

	vec4(0,1,1,1),
	vec4(1,0,1,1),
	vec4(1,1,0,1)
};

uint cube_face(vec3 v)
{
	float c[6] = {v.x,v.y,v.z,-v.x,-v.y,-v.z};

	uint argmax = 0;
	float max = -1e9;

	for (uint i = 0; i < 6; ++i) {
		if (c[i] >= max) {
			argmax = i;
			max = c[i];
		}
	}

	return argmax;
}

mat3 frame()
{
	vec3 dp1 = dFdx(in_pos);
	vec3 dp2 = dFdy(in_pos);

	vec2 duv1 = dFdx(in_uv);
	vec2 duv2 = dFdy(in_uv);

	float det = duv1.x*duv2.y - duv2.x*duv1.y;

	vec3 dSdu = normalize(duv2.y*dp1 - duv1.y*dp2);
	vec3 dSdv = normalize(-duv2.x*dp1 + duv1.x*dp2);

	vec3 N = normalize(cross(dSdu,dSdv));

	return mat3(dSdu, dSdv, N);
}

void main()
{
	vec2 uv = in_uv;

	float r = length(uv - vec2(0.5));
	float f = exp(-16*pow(1-r,4));

	vec3 uvw = vec3(in_uv, in_tex_idx.tex);

	bool valid = is_valid(in_tex_idx);

	if (!valid) 
		discard;

	float val = valid ? texture(u_tex_arrays[in_tex_idx.page], uvw).r : 0;

	vec4 color = mix(vec4(0,0.5,0,1), vec4(0.4,0.45,0.5,1), clamp(20*val,0,1));

	float t = TWOPI*fract(u_frame.t*0.0001);

	vec2 z = vec2(cos(t),sin(t));

	vec3 sun[4] = { 
		vec3(z.x,z.y,z.x),
		normalize(vec3(z.y,-z.x,0.5)),
		vec3(0,z.y,z.x),
		vec3(0,0,1),
	};

	vec4 sun_colors[4] = { 
		vec4(1,0,0,1),
		vec4(0,1,0,1),
		vec4(0,0,1,1),
		vec4(1,1,1,1),
	};

	if (true) {
		//vec3 dx = dFdx(in_pos);
		//vec3 dy = dFdy(in_pos);
		//vec3 n = normalize(cross(dx,dy));
		mat3 TBN = frame();
		vec3 n = in_normal;

		vec3 V = view_dir();

		vec4 diffuse = vec4(0);
		float spec = 0;

		for (int i = 0; i < 4; ++i) {
			vec3 L = sun[i];
			vec4 C = sun_colors[i];

			diffuse += color*C*max(dot(n, L),0);
			
			vec3 R = reflect(L,n);

			spec += pow(max(dot(V,R),0),32);
		}

		spec = min(spec,0.1);

		color = mix(diffuse,mix(color,vec4(0,0,0.5,0),0.5),0.2) + spec*vec4(1);
	}

	FragColor = mix(color,vec4(0,in_uv,1),0.0);
	//FragColor = vec4(in_normal,1);
	//FragColor = mix(FragColor,FACE_COLORS[cube_face(in_pos)],0.0);
	//FragColor = mix(FragColor,vec4(0,in_uv,1),0.0);
}
