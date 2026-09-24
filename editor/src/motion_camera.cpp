#include "ev2/motion_camera.h"

#include "ev2/math_defs.h"
#include "ev2/utils/camera_math.h"

#include "imgui.h"

MotionCamera2::MotionCamera2(glm::dvec3 in_center, glm::dvec3 in_eye, glm::dvec3 in_up) 
{
	glm::vec3 n = glm::normalize(glm::vec3(in_eye - in_center));

	phi = atan2f(n.z,sqrtf(powf(n.x,2) + powf(n.y,2)));
	tht = atan2f(n.y , n.x);

	pos = in_eye;
	up = in_up;
}

void MotionCamera2::update(const CameraInput &input)
{
	glm::vec2 delta = sensitivity * (PIf/180.f) * input.cursor_delta;

	if (input.capture_mouse) {
		rotate(-delta.x, delta.y);
		move(input.dt * speed * input.move_dir);
	}
}

glm::mat4 MotionCamera2::view() const
{
	glm::mat3 TBN = s2_frame((float)phi,(float)tht);
	glm::vec3 v = -pos * TBN;
	glm::mat4 view = glm::mat4(glm::transpose(TBN));
	view[3] = glm::vec4(v,1);

	return view;
}

glm::mat4 MotionCamera2::proj(glm::vec2 size) const
{
	float aspect = (float)size.y/(float)size.x;
	return camera_proj_3d(fov, aspect, far_plane, near_plane);
}

void MotionCamera2::imgui()
{
	ImGui::SliderFloat("Move Speed", &speed, 0, 1.f);
	ImGui::SliderFloat("Sensitivity (deg/pix)", &sensitivity, 0, 1.f);
	ImGui::SliderFloat("Near Plane", &near_plane, 0, 1.f);
	ImGui::SliderFloat("Far Plane", &far_plane, near_plane, 1000.f);
}

const char *MotionCamera2::name() const
{
	return "Motion Camera";
}

void MotionCamera2::rotate(float dtht, float dphi)
{
	float phi1 = phi + dphi;
	phi = glm::clamp(phi1, -HALFPIf, HALFPIf);
	tht = fmodf(tht + dtht, TWOPIf);
}

void MotionCamera2::move(glm::dvec3 motion)
{
	float sin_tht = sinf(tht);
	float cos_tht = cosf(tht);

	glm::dvec3 X = glm::dvec3(-cos_tht, -sin_tht, 0);
	glm::dvec3 Y = glm::dvec3(-sin_tht, cos_tht, 0);
	glm::dvec3 Z = glm::dvec3(0,0,1);

	glm::dvec3 V = X*motion.x + Y*motion.y + Z*motion.z;
	glm::dvec3 v = V;
	pos += v;
}
