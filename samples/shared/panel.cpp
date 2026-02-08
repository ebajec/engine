#include "panel.h"

ev2::RenderTargetID Panel::get_target() {
	return m_target;
}

glm::ivec2 Panel::get_size() {
	return m_size;
}

glm::ivec2 Panel::get_pos()
{
	return m_pos;
}

bool Panel::is_hovered() {
	return m_hovered;
}

bool Panel::is_content_selected() {
	return m_content_selected;
}

Panel::Panel(
	ev2::Device *dev,
	const char *name, 
	uint32_t x, uint32_t y,
	uint32_t w, uint32_t h)
{
	m_dev = dev;

	m_pos = glm::ivec2(x,y);
	m_size = glm::ivec2(w,h);
	m_name = name;
}

Panel::~Panel()
{
	if (EV2_VALID(m_target))
		ev2::destroy_render_target(m_dev, m_target);
}

void Panel::imgui()
{
	ImVec2 content_min = ImGui::GetWindowContentRegionMin(); 
	ImVec2 content_max = ImGui::GetWindowContentRegionMax();

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings;

	if (m_content_selected) 
		flags |= ImGuiWindowFlags_NoMove;

	if (!EV2_VALID(m_target)) {
		ImGui::SetNextWindowPos(ImVec2(m_pos.x, m_pos.y));
		ImGui::SetNextWindowSize(ImVec2(m_size.x, m_size.y));
	}

	ImGui::Begin(m_name.c_str(), NULL, flags);
	ImVec2 imsize = ImGui::GetWindowSize();
	ImVec2 impos = ImGui::GetWindowPos();

	m_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_None);
	m_content_selected = m_hovered && ImGui::GetMousePos().y >= ImGui::GetCursorScreenPos().y;

	m_pos = glm::ivec2(impos.x, impos.y);
	glm::ivec2 size = glm::ivec2(imsize.x,imsize.y);

	if (EV2_IS_NULL(m_target) || 
		m_size != size
	) {
		if (!EV2_IS_NULL(m_target))
			ev2::destroy_render_target(m_dev, m_target);

		m_target = ev2::create_render_target(m_dev, (uint32_t)size.x, (uint32_t)size.y,
						 m_target_flags);
		m_size = size;
	}

	ev2::RenderTargetAttachments attachments = 
		ev2::get_render_target_attachments(m_dev, m_target);
	GLuint gl_tex_handle = ev2::get_texture_gpu_handle(
		m_dev, attachments.color);

	ImVec2 content = ImGui::GetContentRegionAvail();
	ImGui::Image((ImTextureID)(intptr_t)gl_tex_handle, content, ImVec2(0,1), ImVec2(1,0));

	ImGui::End();
}
