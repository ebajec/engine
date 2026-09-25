#ifndef EV2_CAMERA_CONTROLLER_H
#define EV2_CAMERA_CONTROLLER_H

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

struct CameraInput {
 	// viewport local cursor location, pixel coords
    glm::vec2 cursor;

 	// cursor delta, pixel coords
    glm::vec2 cursor_delta;
 	
	// viewport size
    glm::vec2 size;

    float scroll_delta;
    float dt;

    glm::vec3 move_dir;

    bool capture_mouse;
    bool left_down;
	bool right_down;
};

struct ICameraController
{
	virtual ~ICameraController() = default;

	virtual void update(const CameraInput &input) = 0;
	virtual glm::mat4 view() const = 0;
	virtual glm::mat4 proj(glm::vec2 size) const = 0;

	virtual void imgui() {}
	virtual const char *name() const = 0;
	virtual bool want_capture_mouse() const { return false; }
};

#endif //EV2_CAMERA_CONTROLLER_H
