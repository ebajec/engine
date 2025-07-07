#ifndef CAMERA_CONTROLLER_H
#define CAMERA_CONTROLLER_H

#include <glm/vec3.hpp>
#include <glm/matrix.hpp>
#include <glm/ext/quaternion_common.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/quaternion_exponential.hpp>
#include <glm/gtc/quaternion.hpp>

#include <GLFW/glfw3.h>

#include <cfloat>

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

static inline glm::mat4 camera_proj(float fov, float aspect, float far, float near)
{
	float tanfov2 = tanf(fov / 2);
	float ffov = 1.0f/tanfov2;

 	return glm::transpose(glm::mat4(
		glm::vec4(ffov*aspect,0,0,0),
		glm::vec4(0,ffov,0,0),
		glm::vec4(0,0,-(far + near)/(far - near),-2*far*near/(far - near)),
		glm::vec4(0,0,-1,1)
	)); 
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
		glm::mat3 m = glm::transpose(s2_frame((float)phi,(float)tht));
		glm::vec3 v = -m*p;
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

	void handle_key_input(int key, int action) 
	{
		static const int key_fwd = GLFW_KEY_W;
		static const int key_bkwd = GLFW_KEY_S; 
		static const int key_left = GLFW_KEY_A;
		static const int key_right = GLFW_KEY_D;
		static const int key_up = GLFW_KEY_SPACE;
		static const int key_down = GLFW_KEY_LEFT_SHIFT;

		if (key == key_fwd && action == GLFW_PRESS) 
			dir += glm::vec3(1,0,0);
		if (key == key_left && action == GLFW_PRESS) 
			dir += glm::vec3(0,1,0);
		if (key == key_bkwd && action == GLFW_PRESS) 
			dir += glm::vec3(-1,0,0);
		if (key == key_right && action == GLFW_PRESS) 
			dir += glm::vec3(0,-1,0);
		if (key == key_up && action == GLFW_PRESS) 
			dir += glm::vec3(0,0,1);
		if (key == key_down && action == GLFW_PRESS) 
			dir += glm::vec3(0,0,-1);

		if (key == key_fwd && action == GLFW_RELEASE) 
			dir -= glm::vec3(1,0,0);
		if (key == key_left && action == GLFW_RELEASE) 
			dir -= glm::vec3(0,1,0);
		if (key == key_bkwd && action == GLFW_RELEASE) 
			dir -= glm::vec3(-1,0,0);
		if (key == key_right && action == GLFW_RELEASE) 
			dir -= glm::vec3(0,-1,0);
		if (key == key_up && action == GLFW_RELEASE) 
			dir -= glm::vec3(0,0,1);
		if (key == key_down && action == GLFW_RELEASE) 
			dir -= glm::vec3(0,0,-1);
	}

	void update() 
	{
		double sin_tht = sin(tht);
		double cos_tht = cos(tht);

		glm::dvec3 X = glm::vec3(-cos_tht, -sin_tht, 0);
		glm::dvec3 Y = glm::vec3(sin_tht, -cos_tht, 0);
		glm::dvec3 Z = glm::vec3(0,0,1);

		glm::dvec3 V = X*dir.x + Y*dir.y + Z*dir.z;
		double norm = glm::length(V);

		glm::dvec3 v = norm > DBL_EPSILON ? V/norm : glm::dvec3(0);

		v *= 0.1;

		p += v;
	}
};
#endif //CAMERA_CONTROLLER_H

