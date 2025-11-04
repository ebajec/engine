#ifndef COMMON_GLSL
#define COMMON_GLSL

#define PI 3.141592654
#define TWOPI 6.28318530718

struct quat {
	float w,x,y,z;
};

vec3 hsv2rgb(vec3 c)
{
    // c.x = H in [0,1], c.y = S in [0,1], c.z = V in [0,1]
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.x + K.xyz) * 6.0 - K.www);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}
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

vec4 qexp(vec3 v, float t)
{
	t *= 0.5;
	v *= sin(t);

	vec4 q;
	q.w = cos(t);
	q.x = v.x;
	q.y = v.y;
	q.z = v.z;

	return q;
}

vec4 qmult(vec4 a, vec4 b)
{
    vec3 av = a.xyz, bv = b.xyz;
    float aw = a.w,   bw = b.w;

    vec3 v = aw * bv + bw * av + cross(av, bv);
    float w = aw * bw - dot(av, bv);
    return vec4(v, w);
}

vec4 qconj(vec4 q)
{
	vec4 qp;
	qp.w = q.w; 
	qp.x = -q.x; 
	qp.y = -q.y; 
	qp.z = -q.z; 

	return qp;
}

#endif // COMMON_GLSL
