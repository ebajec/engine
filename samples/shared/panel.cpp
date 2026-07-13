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
	uint32_t w, uint32_t h,
	ev2::RenderTargetFlags flags
)
{
	m_ctx = ctx;
	m_app = app;

	m_target_flags = flags;

	m_pos = glm::ivec2(x,y);
	m_size = glm::ivec2(w,h);
	m_name = name;
	m_settings_name = m_name + "_Settings";
}

Panel::~Panel()
{
	if (m_target.is_valid()) {
		ev2::destroy_render_target(m_ctx, m_target);
	}
}

int Panel::update(bool *was_resized)
{
	if (m_needs_resize) {
		if (m_target.is_valid()) {
			m_app->release_image_for_gui(
				ev2::get_render_target_color_image(m_target));
			ev2::destroy_render_target(m_ctx, m_target);
			imgui_texture = VK_NULL_HANDLE;
			m_target = EV2_NULL_HANDLE(RenderTarget);
		}

		if (m_size.x <= 0 || m_size.y <= 0) {
			return App::OK;
		}

		m_target = ev2::create_render_target(m_ctx, (uint32_t)m_size.x, (uint32_t)m_size.y,
						 m_target_flags);
		if (!m_target.is_valid()) {
			return App::ERROR;
		}

		ev2::ImageID color_img = ev2::get_render_target_color_image(m_target);

		VkImageView view = ev2::get_render_target_color_view(m_target);
		imgui_texture = ImGui_ImplVulkan_AddTexture(
			view,
			VK_IMAGE_LAYOUT_GENERAL
		);

		ev2::pre_destroy_callback(m_app->ctx, color_img, [tex = imgui_texture]{
			ImGui_ImplVulkan_RemoveTexture(tex);
		});

		m_app->acquire_image_for_gui(color_img);

		if (was_resized)
			*was_resized = true;
		m_needs_resize = false;
	}

	return App::OK;
}

bool Panel::imgui()
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

	ImGui::SetNextWindowPos(ImVec2(m_pos.x, m_pos.y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(m_size.x, m_size.y), ImGuiCond_FirstUseEver);

	bool open = true;

	if (ImGui::Begin(m_name.c_str(), m_closable ? &open : nullptr, ImGuiWindowFlags_MenuBar)) {
		if (ImGui::BeginMenuBar()) {
			float button_w = ImGui::GetFrameHeight();
			ImGui::SetCursorPosX(ImGui::GetWindowWidth() - button_w - ImGui::GetStyle().ItemSpacing.x);

			if (ImGui::Button("...##settings", ImVec2(button_w, 0))) {
				ImGui::OpenPopup("panel_settings");
			}
			if (ImGui::BeginPopup("panel_settings")) {
				ImGui::Text("Settings");
				if (settings_callback)
					settings_callback();
				ImGui::EndPopup();
			}
			ImGui::EndMenuBar();
		}

		if (!ImGui::IsWindowAppearing()) {
			ImVec2 cursor = ImGui::GetCursorScreenPos();
			ImVec2 content = ImGui::GetContentRegionAvail();

			m_hovered = ImGui::IsWindowHovered();
			m_focused = ImGui::IsWindowFocused(); 

			m_content_hovered = m_hovered && 
				ImGui::GetMousePos().y >= cursor.y;

			if (m_content_hovered) {
				ImGui::GetCurrentWindow()->Flags |= ImGuiWindowFlags_NoMove;
			}

			m_pos = glm::ivec2(cursor.x, cursor.y);
			glm::ivec2 size = glm::max(glm::ivec2(content.x,content.y), glm::ivec2(0));

			if (m_size != size) {
				m_needs_resize = true;
			}
			m_size = size;

			// If the window is closed and this code executes, the frame will be left holding
			// an invalid image id since the render target gets destroyed
			if (open && imgui_texture) {
				ImGui::ImageWithBg(
					(ImTextureID)imgui_texture, 
					content, 
					ImVec2(0,1), 
					ImVec2(1,0), 
					ImVec4(0.f,0.f,0.f,0.f), 
					ImVec4(1.f,1.f,1.f,1.f)
				);
			}
		}
	}
	ImGui::End();

	return open;
}
