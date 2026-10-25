#include "ev2/spherical_motion_camera.h"

#include "ev2/math_defs.h"
#include "ev2/utils/camera_math.h"

#include "imgui.h"

namespace ev2 {

void SphericalMotionCamera2::update(const CameraInput &input)
{
	if (!input.capture_mouse || input.size.x <= 0)
		return;

	glm::dvec2 delta = glm::dvec2(sensitivity * input.cursor_delta / input.size.x);
	rotate(-delta.y, delta.x);

	float norm = length(input.move_dir);
	if (norm > 1e-3f) {
		double s = (double)speed;
		if (scale_speed)
			s *= std::min(0.001 + std::max(altitude(), 0.), 1.);
		move((double)input.dt * s * glm::dvec3(input.move_dir / norm));
	}
}

glm::mat4 SphericalMotionCamera2::view() const
{
	glm::vec3 fwd = glm::normalize(glm::cross(up,right));

	float sin_phi = sinf((float)phi);
	float cos_phi = cosf((float)phi);

	glm::vec3 T = right;
	glm::vec3 B = cos_phi*glm::vec3(up) - sin_phi*fwd;
	glm::vec3 N = glm::normalize(glm::cross(T,B));

	glm::mat3 m = glm::transpose(glm::mat3(T,B,N));

	glm::vec3 c = -m*glm::vec3(get_pos());

	glm::mat4 view = glm::mat4(m);
	view[3] = glm::vec4(c,1);

	return view;
}

glm::mat4 SphericalMotionCamera2::proj(glm::vec2 size) const
{
	float aspect = (float)size.y/(float)size.x;
	return camera_proj_3d(fov * (PIf/180.f), aspect, far_plane, near_plane);
}

void SphericalMotionCamera2::imgui()
{
	ImGui::BeginChild("FixedWidthWrapper", ImVec2(250, 0), ImGuiChildFlags_AutoResizeY);
	if (ImGui::CollapsingHeader("Camera")) {
		ImGui::Indent();
		ImGui::SliderFloat("Move Speed", &speed, 0, 3.f);
		ImGui::Checkbox("Slow Down Near Surface", &scale_speed);
		ImGui::SliderFloat("Sensitivity (rad/width)", &sensitivity, 0, 4.f);
		ImGui::SliderFloat("FOV (deg)", &fov, 1, 179.f);

		if (ImGui::Checkbox("Auto Planes", &auto_planes))
			update_planes();
		ImGui::BeginDisabled(auto_planes);
		ImGui::SliderFloat("Near Plane", &near_plane, 1e-5f, 1.f, "%.5f",
			ImGuiSliderFlags_Logarithmic);
		ImGui::SliderFloat("Far Plane", &far_plane, near_plane, 100.f, "%.3f",
			ImGuiSliderFlags_Logarithmic);
		ImGui::EndDisabled();

		ImGui::Text("Altitude: %.6f", altitude());
		ImGui::Unindent();
	}
	ImGui::EndChild();
}

const char *SphericalMotionCamera2::name() const
{
	return "Spherical Motion Camera";
}

void SphericalMotionCamera2::move(glm::dvec3 motion)
{
	height = std::max(1. + min_height,height + motion.z);

	glm::dvec3 fwd = glm::normalize(glm::cross(up,right));

	glm::dvec3 v = motion.x * fwd + motion.y * right;
	double dist = length(v);

	if (fabs(dist) > DBL_EPSILON) {
		v /= dist;

		glm::dquat q = quat_exp(glm::normalize(cross(up,v)),(float)dist);
		glm::dquat qstar = glm::conjugate(q);

		glm::dquat q_up = q*glm::dquat(0,up)*qstar;
		glm::dquat q_right = q*glm::dquat(0,right)*qstar;

		up = glm::normalize(glm::dvec3(q_up.x,q_up.y,q_up.z));
		right = glm::dvec3(q_right.x,q_right.y,q_right.z);

		// not required, but helps avoid precision loss over time
		right = glm::normalize(right - up*dot(right,up));
	}

	update_planes();
}

void SphericalMotionCamera2::rotate(double dphi, double dtht)
{
	phi = glm::clamp(phi + dphi,-HALFPI,HALFPI);

	glm::dquat q = quat_exp(up,(float)-dtht);
	glm::dquat a_new = q*glm::dquat(0,right)*glm::conjugate(q);

	right = glm::dvec3(a_new.x,a_new.y,a_new.z);
}

void SphericalMotionCamera2::set_min_height(double h)
{
	min_height = h;
	height = std::max(1. + min_height, height);

	update_planes();
}

void SphericalMotionCamera2::update_planes()
{
	if (!auto_planes)
		return;

	float h = (float)std::max(altitude(), 0.);

	near_plane = glm::clamp(0.25f*h, 0.0001f, 0.02f);
	far_plane = glm::clamp(1000.f*near_plane, 1.f, 2.f);
}

} // namespace ev2
