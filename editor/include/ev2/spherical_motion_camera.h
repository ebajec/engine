#ifndef EV2_SPHERICAL_MOTION_CAMERA_H
#define EV2_SPHERICAL_MOTION_CAMERA_H

#include "ev2/camera_controller.h"

namespace ev2 {

// Camera that moves over the surface of the unit sphere. 'up' is the surface
// normal under the camera and 'right' the horizontal right direction; 'phi'
// tilts the view from looking along the surface towards straight down/up.
struct SphericalMotionCamera2 : public ICameraController
{
	// degrees for slider readability
	float fov = 90.f;
	// radians per viewport width
	float sensitivity = 1.f;
	float speed = 1.f;
	// scale speed by altitude, so motion slows down near the surface
	bool scale_speed = false;

	float near_plane = 0.02f;
	float far_plane = 2.f;
	// derive near_plane and far_plane from altitude when it changes
	bool auto_planes = true;

	glm::dvec3 up = glm::dvec3(1,0,0);
	glm::dvec3 right = glm::dvec3(0,1,0);
	double phi = 0;

	double min_height = 0.01;
	double height = 2.1;

	SphericalMotionCamera2() = default;

	virtual void update(const CameraInput &input) final;
	virtual glm::mat4 view() const final;
	virtual glm::mat4 proj(glm::vec2 size) const final;

	virtual void imgui() final;
	virtual const char *name() const final;

	virtual bool want_capture_mouse() const final { return true; }

	void move(glm::dvec3 motion);
	void rotate(double dphi, double dtht);

	glm::dvec3 get_pos() const { return height * up; }

	// height above the 'min_height' surface
	double altitude() const { return height - 1. - min_height; }

	// @param h minimum height above the unit sphere, e.g. terrain elevation
	// under the camera
	void set_min_height(double h);

	void update_planes();
};

} // namespace ev2

#endif // EV2_SPHERICAL_MOTION_CAMERA_H
