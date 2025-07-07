#define PI 3.141592654
#define TWOPI 6.28318530718

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

