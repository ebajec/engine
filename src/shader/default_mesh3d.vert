#version 430 core

struct framedata_t
{
	mat4 p;
	mat4 v;
	mat4 pv;
	float t;
};

layout (binding = 5) uniform Framedata
{
	framedata_t u_frame;
};

#define PI 3.141592654

//--------------------------------------------------------------------------------------------------
// Vert

layout (binding = 0) uniform sampler2D u_tex;

layout (location = 0) in vec3 pos;
layout (location = 1) in vec2 uv;
layout (location = 2) in vec3 normal;

layout (location = 0) out vec3 frag_pos;
layout (location = 1) out vec2 frag_uv;
layout (location = 2) out vec3 frag_normal;

struct quat {
	float w,x,y,z;
};

mat3 qmat3(quat q) 
{
    float w=q.w, x=q.x, y=q.y, z=q.z;
    float xx=x*x, yy=y*y, zz=z*z;
    float xy=x*y, xz=x*z, yz=y*z;
    float wx=w*x, wy=w*y, wz=w*z;

    return mat3(
      1-2*(yy+zz),   2*(xy - wz), 2*(xz + wy),
      2*(xy + wz), 1-2*(xx+zz),   2*(yz - wx),
      2*(xz - wy),   2*(yz + wx), 1-2*(xx+yy)
    );
}

quat qexp(vec3 v, float t)
{
	t *= 0.5;
	v *= sin(t);

	quat q;
	q.w = cos(t);
	q.x = v.x;
	q.y = v.y;
	q.z = v.z;

	return q;
}

void main()
{
	float t = u_frame.t;
	mat4 pv = u_frame.pv;

	mat4 m = mat4(qmat3(qexp(normalize(vec3(1,0,1)),t)));

	vec4 wpos = m * vec4(pos, 1);
	vec4 n = m * vec4(normal,0);

	vec4 c = texture(u_tex,uv);

	float w1 = c.r; 
	float w2 = c.g; 
	float w3 = c.b; 

	float val = 10*wpos.z*wpos.y*wpos.x*sin(4.0*t);
	
	wpos += (1 + 0.5*val)*n;

	frag_pos = wpos.xyz;
	frag_uv = uv;
	frag_normal = n.xyz;

	gl_Position = (pv*wpos);
}

