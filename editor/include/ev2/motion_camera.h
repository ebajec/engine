#ifndef EV2_MOTION_CAMERA_H
#define EV2_MOTION_CAMERA_H

#include "ev2/camera_controller.h"

struct MotionCamera : public ICameraController
{
	// degrees for slider readability
	float fov = 60.f;
	float sensitivity = 0.05f;
	float far_plane = 1e2f;
	float near_plane = 1e-3f;
	float speed = 1.0;

	float tht = 0.0;
	float phi = 0.0;

	glm::vec3 up = glm::dvec3(0,0,1);

	// only the position is kept as a double 
	glm::dvec3 pos = glm::dvec3(0,0,0);

	MotionCamera() = default;

	// @brief Initialize the camera looking from 'eye' to 'center'
	MotionCamera(glm::dvec3 in_center, glm::dvec3 in_eye, glm::dvec3 in_up); 

	virtual void update(const CameraInput &input) final;
	virtual glm::mat4 view() const final;
	virtual glm::mat4 proj(glm::vec2 size) const final;

	virtual void imgui() final;
	virtual const char *name() const final;

	virtual bool want_capture_mouse() const final { return true; }

	void rotate(float dtht, float dphi);
	void move(glm::dvec3 motion); 
};

#endif //EV2_MOTION_CAMERA_H
