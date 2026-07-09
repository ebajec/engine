#include "panel.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui_internal.h"

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
	return m_content_hovered && m_focused;
}

bool Panel::is_focused() {
	return m_focused;
}

Panel::Panel(
	App * app,
	ev2::GfxContext *ctx,
	const char *name, 
	uint32_t x, uint32_t y,
	uint32_t w, uint32_t h)
{
	m_ctx = ctx;
	m_app = app;

	m_pos = glm::ivec2(x,y);
	m_size = glm::ivec2(w,h);
	m_name = name;
}

Panel::~Panel()
{
	if (EV2_VALID(m_target))
		ev2::destroy_render_target(m_ctx, m_target);
}

void Panel::imgui()
{
	if (ImGuiWindow* window = ImGui::FindWindowByName(m_name.c_str())) {
		bool isDraggingThisWindow = ImGui::GetCurrentContext()->MovingWindow == window;

		if (isDraggingThisWindow) {
			ImVec2 pos = window->Pos;
			ImVec2 size = window->Size;
			ImVec2 displaySize = ImGui::GetIO().DisplaySize;

			ImVec2 clamped = pos;
			clamped.y = ImMax(pos.y, 0.0f);

			if (clamped.y != pos.y) {
				// Nudge the click offset so the cursor doesn't desync from the window
				ImGuiContext& g = *ImGui::GetCurrentContext();
				g.ActiveIdClickOffset.y += pos.y - clamped.y; 
				window->Pos = clamped;
			}
		}

		glm::ivec2 size = glm::ivec2(window->Size.x, window->Size.y);
	}

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	if (ImGui::Begin(m_name.c_str(), NULL)) {
		ImVec2 win_size = ImGui::GetWindowSize();
		ImVec2 win_pos = ImGui::GetWindowPos();

		m_hovered = ImGui::IsWindowHovered();
		m_focused = ImGui::IsWindowFocused(); 

		m_content_hovered = m_hovered && 
			ImGui::GetMousePos().y >= ImGui::GetCursorScreenPos().y;

		if (m_content_hovered) {
			ImGui::GetCurrentWindow()->Flags |= ImGuiWindowFlags_NoMove;
		}

		m_pos = glm::ivec2(win_pos.x, win_pos.y);
		glm::ivec2 size = glm::ivec2(win_size.x,win_size.y);

		if ((EV2_IS_NULL(m_target) || m_size != size)) {
			if (!EV2_IS_NULL(m_target))
				ev2::destroy_render_target(m_ctx, m_target);

			m_target = ev2::create_render_target(m_ctx, (uint32_t)size.x, (uint32_t)size.y,
							 m_target_flags);
			m_size = size;

			VkImageView view = ev2::get_render_target_color_view(m_target);
			ImGui_ImplVulkan_RemoveTexture(imgui_texture);
			imgui_texture = ImGui_ImplVulkan_AddTexture(
				view,
				VK_IMAGE_LAYOUT_GENERAL
			);
		}
		m_app->use_image_for_gui(ev2::get_render_target_color_image(m_target));
		ImVec2 content = ImGui::GetContentRegionAvail();
		ImGui::Image((ImTextureID)imgui_texture, content, ImVec2(0,1), ImVec2(1,0));
	}
	ImGui::End();
	ImGui::PopStyleColor();
}
