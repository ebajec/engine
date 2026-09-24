#ifndef EV2_PANNING_CAMERA_H
#define EV2_PANNING_CAMERA_H

#include "ev2/camera_controller.h"

struct PanningCamera : public ICameraController
{
	double zoom = 1.0;
	glm::dvec2 pos = glm::dvec2(0,0);

	PanningCamera() = default;
	PanningCamera(glm::dvec2 in_pos, double in_zoom) : zoom(in_zoom), pos(in_pos) {}

	virtual void update(const CameraInput &input) final;
	virtual glm::mat4 view() const final;
	virtual glm::mat4 proj(glm::vec2 size) const final;

	virtual const char *name() const final;

	virtual void imgui() final;
};
#endif // EV2_PANNING_CAMERA_H
