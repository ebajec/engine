#include "ev2/panning_camera.h"
#include "ev2/utils/camera_math.h"

#include "ev2/utils/log.h"

void PanningCamera::update(const CameraInput &input)
{
	// Panning should do nothing when cursor is disabled
	if (input.capture_mouse)
		return;

	constexpr double alpha = 2.0;

	glm::dvec2 size = input.size;

	double scale = size.y * zoom;

	glm::dvec2 norm_cursor = 2.0 * glm::dvec2(
		(double)input.cursor.x - 0.5 * size.x,
		0.5 * size.y - (double)input.cursor.y) / size.y;

	glm::dvec2 world_pos = pos + norm_cursor / zoom;
	zoom *= pow(alpha, (double)input.scroll_delta); 
	pos = world_pos - norm_cursor / zoom;

	if (input.left_down) {
		glm::dvec2 delta = glm::dvec2(input.cursor_delta);
		pos -= 2.0 * glm::dvec2(delta.x, -delta.y) / scale;
	}
}

glm::mat4 PanningCamera::view() const
{
	glm::mat4 view = glm::mat4(1.f);
	view[3] = glm::vec4(-pos, 0, 1);

	return view;
}

glm::mat4 PanningCamera::proj(glm::vec2 size) const
{
	float aspect = (float)size.y/(float)size.x;
	return camera_proj_2d(aspect, (float)zoom);
}


void PanningCamera::imgui()
{
}

const char *PanningCamera::name() const
{
	return "Panning Camera";
}

