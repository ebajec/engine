#ifndef CAMERA_CONTROLLER_H
#define CAMERA_CONTROLLER_H

#include <glm/vec3.hpp>
#include <glm/matrix.hpp>
#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/quaternion_exponential.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cfloat>
#include <algorithm>

#ifndef HALFPIf
#define HALFPIf 1.57079632679f
#endif

#ifndef HALFPI
#define HALFPI 1.57079632679
#endif

#ifndef PI
#define PI 3.14159265359
#endif

#ifndef PIf
#define PIf 3.14159265359f
#endif

#ifndef TWOPI
#define TWOPI (2.0*3.141592654)
#endif

static inline glm::mat3 quat_to_mat3(const glm::quat& q) {
    float w=q.w, x=q.x, y=q.y, z=q.z;
    float xx=x*x, yy=y*y, zz=z*z;
    float xy=x*y, xz=x*z, yz=y*z;
    float wx=w*x, wy=w*y, wz=w*z;

    return glm::mat3{
      {1-2*(yy+zz),   2*(xy - wz), 2*(xz + wy)},
      {2*(xy + wz), 1-2*(xx+zz),   2*(yz - wx)},
      {2*(xz - wy),   2*(yz + wx), 1-2*(xx+yy)}
    };
}

static inline glm::quat quat_exp(glm::vec3 v, float t)
{
	t *= 0.5f;
	v *= sinf(t);
	return glm::quat(cosf(t),v);
}

static inline glm::mat4 camera_proj_3d(float fov, float aspect, float far, float near)
{
	float tanfov2 = tanf(fov / 2);
	float ffov = 1.0f/tanfov2;

	far = std::max(far, near + 64*FLT_EPSILON);

	float d = far - near;

	glm::mat4 proj = glm::transpose(glm::mat4(
		glm::vec4(ffov*aspect,0,0,0),
		glm::vec4(0,ffov,0,0),
		glm::vec4(0,0,-(far)/d,-far*near/d),
		glm::vec4(0,0,-1,0)
	)); 

	return proj;
}

static inline glm::mat4 camera_proj_2d(float aspect, float scale)
{
 	return glm::transpose(glm::mat4(
		glm::vec4(scale*aspect,0,0,0),
		glm::vec4(0,scale,0,0),
		glm::vec4(0,0,1,0),
		glm::vec4(0,0,-1,1)
	)); 
}

static inline glm::vec3 rotate(glm::vec3 v, glm::vec3 axis, float tht)
{
	glm::quat q = quat_exp(axis,tht);
	glm::quat q2 = q*glm::quat(0,v)*glm::conjugate(q);

	return glm::vec3(q2.x,q2.y,q2.z);
}

static inline glm::vec3 camera_get_pos(glm::mat4 const& view) 
{
	return -glm::vec3(view[3])*glm::mat3(view);

}

static inline glm::mat3 s2_frame(float phi, float theta) 
{
	float cos_phi = cosf(phi);
	float cos_tht = cosf(theta);
	float sin_phi = sinf(phi);
	float sin_tht = sinf(theta);

	glm::vec3 T = glm::vec3(-sin_tht, cos_tht,0);
	glm::vec3 B = glm::vec3(-sin_phi*cos_tht,-sin_phi*sin_tht,cos_phi);
	glm::vec3 N = glm::vec3(cos_phi*cos_tht,cos_phi*sin_tht,sin_phi);

	return glm::mat3(T,B,N);
}

static inline glm::vec2 screen_to_world_2d(const glm::dmat4& view, const glm::dmat4& proj, glm::vec2 screen) 
{
	return glm::inverse(proj*view)*glm::vec4(screen,0,0);
}

struct SphericalMotionCamera
{
	glm::dvec3 up = glm::dvec3(1,0,0);
	glm::dvec3 right = glm::dvec3(0,1,0);
	double phi = 0;

	double min_height = 0.01;
	double height = 2.1;

	void move(glm::dvec3 motion)
	{
		height = std::max(1. + min_height,height + motion.z);

		glm::dvec3 fwd = glm::normalize(glm::cross(up,right));

		// TODO: This is not
		glm::dvec3 v = motion.x * fwd + motion.y * right;
		double speed = length(v);

		if (fabs(speed) > DBL_EPSILON) {
			v /= speed;

			glm::dquat q = quat_exp(glm::normalize(cross(up,v)),(float)speed);
			glm::dquat qstar = glm::conjugate(q);

			glm::dquat q_up = q*glm::dquat(0,up)*qstar;
			glm::dquat q_right = q*glm::dquat(0,right)*qstar;

			up = glm::normalize(glm::dvec3(q_up.x,q_up.y,q_up.z));
			right = glm::dvec3(q_right.x,q_right.y,q_right.z);

			// not required, but helps avoid precision loss over time
			right = glm::normalize(right - up*dot(right,up));
		}
	}

	void rotate(double dphi, double dtht) {
		phi = glm::clamp(phi + dphi,-HALFPI,HALFPI);

		glm::dquat q = quat_exp(up,(float)-dtht);
		glm::dquat a_new = q*glm::dquat(0,right)*glm::conjugate(q);

		right = glm::dvec3(a_new.x,a_new.y,a_new.z);
	}

	glm::dvec3 get_pos()
	{
		return height*up;
	}

	void set_min_height(double h)
	{
		min_height = h;
		height = std::max(1. + min_height, height);
	}

	glm::mat4 get_view()
	{
		glm::vec3 fwd = glm::normalize(glm::cross(up,right));

		float sin_phi = sinf((float)phi);
		float cos_phi = cosf((float)phi);

		glm::vec3 T = right;
		glm::vec3 B = cos_phi*glm::vec3(up) - sin_phi*fwd;
		glm::vec3 N = glm::normalize(glm::cross(T,B));

		glm::mat3 m = glm::transpose(glm::mat3(T,B,N));

		glm::vec3 c = -m*(height*up);

		glm::mat4 view = glm::mat4(m);
		view[3] = glm::vec4(c,1);

		return view;
	}
};


struct MotionCamera
{
	glm::dvec3 up;
	glm::dvec3 p;

	glm::dvec3 dir = glm::vec3(0);

	double phi;
	double tht;

	static MotionCamera from_normal(glm::dvec3 n, glm::dvec3 p) 
	{
		MotionCamera cam {};

		cam.phi = atan2(n.z,sqrt(pow(n.x,2) + pow(n.y,2)));
		cam.tht = atan2(n.y , n.x);

		cam.p = p;
		cam.up = glm::dvec3(0,0,1);

		return cam;
	}

	glm::mat4 get_view()
	{
		glm::mat3 TBN = s2_frame((float)phi,(float)tht);
		glm::mat3 m = glm::transpose(TBN);
		glm::vec3 v = -m*(p);
		glm::mat4 view = glm::mat4(m);
		view[3] = glm::vec4(v,1);

		return view;
	}

	void rotate(double dtht, double dphi)
	{
		double phi1 = phi + dphi;
		phi = glm::clamp(phi1, -HALFPI, HALFPI);
		tht = fmod(tht + dtht, TWOPI);
	};

	void update(glm::dvec3 vloc) 
	{
		double sin_tht = sin(tht);
		double cos_tht = cos(tht);

		glm::dvec3 X = glm::vec3(-cos_tht, -sin_tht, 0);
		glm::dvec3 Y = glm::vec3(-sin_tht, cos_tht, 0);
		glm::dvec3 Z = glm::vec3(0,0,1);

		glm::dvec3 V = X*vloc.x + Y*vloc.y + Z*vloc.z;
		double norm = glm::length(V);

		glm::dvec3 v = norm > DBL_EPSILON ? V/norm : glm::dvec3(0);

		v *= 0.1;

		p += v;
	}
};


#endif //CAMERA_CONTROLLER_H

