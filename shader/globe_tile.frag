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

	float s = 1.0 + 2000*val; 
	vec4 color = 
	mix(
		mix(
			vec4(1,0.0,0,1),
			vec4(0,0.5,0,1),
			clamp(2*s,0,1)
		), 
		vec4(0.4,0.45,0.5,1), 
		clamp(2*s - 1,0,1)
	);

	//color = in_color;

	float t = TWOPI*fract(u_frame.t*0.001);

	vec2 z = vec2(cos(t),sin(t));

	vec3 sun[4] = { 
		vec3(z.x,z.y,z.x),
		normalize(vec3(z.y,-z.x,0.5)),
		vec3(0,z.y,z.x),
		vec3(0,0,1),
	};

	vec4 sun_colors[4] = { 
		0.3*vec4(1,1,1,1),
		0.3*vec4(1,1,1,1),
		0.3*vec4(1,1,1,1),
		vec4(1,1,1,1),
	};

	if (true) {
		vec3 n = in_normal;

		vec3 V = view_dir();

		vec4 diffuse = vec4(0);
		float spec = 0;

		for (int i = 0; i < 4; ++i) {
			vec3 L = sun[i];
			vec4 C = sun_colors[i];

			float f = max(dot(n, L),0);

			diffuse += color*C*f;
			
			vec3 R = reflect(L,n);

			spec += pow(max(dot(V,R),0),32);
		}

		spec = min(spec,0.1);

		vec4 ambient = mix(color,vec4(0,0,0.05,0),0.5); 
		color = clamp(mix(diffuse,ambient,0.1),
				vec4(0),color) 
				+ spec*vec4(1);
	}

	FragColor = mix(color,vec4(0,in_uv,1),0.00);
	//FragColor = vec4(in_normal,1);
	//FragColor = mix(FragColor,FACE_COLORS[cube_face(in_pos)],0.0);

	//vec2 diff = 2.0*in_uv - vec2(1.0);
	//FragColor = FragColor * dot(diff,diff);
}
