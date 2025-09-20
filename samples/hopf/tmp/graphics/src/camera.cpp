#include "camera.h"
#include <complex>
#include <cstring>
#include "glm/gtc/type_ptr.hpp"
#include "glm/matrix.hpp"
#include "misc.h"

#define SIZE_F_MAT4 16

static mat3 orthCoordsLeft(vec3 normal)
{
	vec3 basis[3];

	normal = normalize(normal);
	basis[2] = normal;
	basis[0] = cross(normal, vec3({ 0,0,1 }));
	basis[1] = cross(basis[0], basis[2]);
	basis[0] = normalize(basis[0]);
	basis[1] = normalize(basis[1]);

	return mat3(basis[0],basis[1],basis[2]);
}

Camera::Camera(vec3 normal, vec3 pos, int w, int h, GLfloat FOV, GLfloat near, GLfloat far)
	:
	fov(FOV),
	near(near),
	far(far),
	position(pos)
{
	glGenBuffers(1, &ubo);
	coords = mat4(orthCoordsLeft(normal));
	aspect = (float)h / (float)w;

	updateUbo();
}

Camera::~Camera()
{
	glDeleteBuffers(1,&ubo);
}

Camera& Camera::operator=(const Camera& other)
{
	this->fov = other.fov;
 	this->coords = other.coords;
 	this->near = other.near;
 	this->aspect = other.aspect;
 	this->far = other.far;
 	this->position = other.position;
 	this->updateUbo();
 	return *this;
}

mat4 Camera::getViewMatrix() const
{
	mat4 world = mat4(1.0f);
	vec3 viewPos = position - vec3(coords[2]) * near;
	world[3] = vec4(-1.0f *viewPos,1);
	return glm::transpose(coords)*world;;
}

mat4 Camera::getProjMatrix() const
{
	return mat4(
		vec4((1 / tan(fov / 2))*(aspect),0,0,0),
		vec4(0,1 / tan(fov / 2),0,0),
		vec4(0,0,far/(far - near),1),
		vec4(0,0,-far*near/(far - near),0)
	);
}

void Camera::updateUbo()
{	
	mat4 view = getViewMatrix();
	mat4 proj = getProjMatrix();

	CameraUBOLayout data 
	{
		.view = view,
		.proj = proj,
		.pv = proj * view,
		.cam_pos = vec4(position,1),
		.cam_dir = view[2],
		.near = near,
		.far = far
	};

	glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(CameraUBOLayout), &data, GL_STATIC_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER,0);

	return;
}
void Camera::bindUbo(GLuint binding) const
{
	glBindBufferBase(GL_UNIFORM_BUFFER,binding,ubo);
}

void Camera::rotate(float pitch, float yaw)
{
	if (fabs(pitch) > PI) return;
	coords = mat4(rotation(vec3(coords[0]), -pitch) * rotation(vec3(0,0,1),-yaw)) * coords;
}

void Camera::translate(vec3 delta, float speed)
{
	float mag = glm::dot(delta,delta);

	mat3 offsets = mat3(vec3(coords[0]),vec3(0,0,1),vec3(vec2(coords[2]),0));
	
	if (mag) position = position + speed*normalize(offsets*delta);
}

void Camera::resize(int width, int height)
{
	this->aspect = (float)height/(float)width;
}