#include "ev2/motion_camera.h"

#include "ev2/math_defs.h"
#include "ev2/utils/camera_math.h"

#include "imgui.h"

#include <cstdio>

MotionCamera::MotionCamera(glm::dvec3 in_center, glm::dvec3 in_eye, glm::dvec3 in_up) 
{
	glm::vec3 n = glm::normalize(glm::vec3(in_eye - in_center));

	phi = atan2f(n.z,sqrtf(powf(n.x,2) + powf(n.y,2)));
	tht = atan2f(n.y , n.x);

	pos = in_eye;
	up = in_up;
}

void MotionCamera::update(const CameraInput &input)
{
	glm::vec2 delta = sensitivity * (PIf/180.f) * input.cursor_delta;

	if (input.capture_mouse) {
		rotate(-delta.x, delta.y);
		float norm = length(input.move_dir);

		if (norm > 1e-3f) {
			move(input.dt * speed * input.move_dir / norm);
		}
	}
}

glm::mat4 MotionCamera::view() const
{
	glm::mat3 TBN = s2_frame((float)phi,(float)tht);
	glm::vec3 v = -pos * TBN;
	glm::mat4 view = glm::mat4(glm::transpose(TBN));
	view[3] = glm::vec4(v,1);

	return view;
}

glm::mat4 MotionCamera::proj(glm::vec2 size) const
{
	float aspect = (float)size.y/(float)size.x;
	return camera_proj_3d(fov * (PIf/180.f), aspect, far_plane, near_plane);
}

void MotionCamera::imgui()
{
	ImGui::BeginChild("FixedWidthWrapper", ImVec2(250, 0), ImGuiChildFlags_AutoResizeY);
	char namebuf[100];
	snprintf(namebuf, sizeof(namebuf), "Camera");
	if (ImGui::CollapsingHeader(namebuf)) {
		ImGui::Indent();
		ImGui::SliderFloat("Move Speed", &speed, 0, 1.f);
		ImGui::SliderFloat("Sensitivity (deg/pix)", &sensitivity, 0, 1.f);
		ImGui::SliderFloat("FOV (deg)", &fov, 1, 179.f);
		ImGui::SliderFloat("Near Plane", &near_plane, 0, 1.f);
		ImGui::SliderFloat("Far Plane", &far_plane, near_plane, 1000.f);
		ImGui::Unindent();
	}
	ImGui::EndChild();
}

const char *MotionCamera::name() const
{
	return "Motion Camera";
}

void MotionCamera::rotate(float dtht, float dphi)
{
	float phi1 = phi + dphi;
	phi = glm::clamp(phi1, -HALFPIf, HALFPIf);
	tht = fmodf(tht + dtht, TWOPIf);
}

void MotionCamera::move(glm::dvec3 motion)
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
