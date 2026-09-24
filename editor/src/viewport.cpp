#include "ev2/editor.h"

#include "ev2/viewport.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui_internal.h"

#include <format>
#include <atomic>

ev2::RenderTargetID Viewport2::get_target() {
	return m_target;
}

static std::atomic_uint32_t g_ctr = 0;

Viewport2::Viewport2(
	const char *name, 
	uint32_t x, uint32_t y,
	uint32_t w, uint32_t h,
	ev2::RenderTargetFlags flags
)
{
	m_id = ++g_ctr;

	m_target_flags = flags;

	m_view = ev2::create_view(Editor::ctx(), nullptr, nullptr);

	m_pos = glm::ivec2(x,y);
	m_size = glm::ivec2(w,h);
	m_name = name ? name : std::format("Viewport2{}", m_id);
}

Viewport2::~Viewport2()
{
	cleanup_render_target();
	ev2::destroy_view(Editor::ctx(), m_view);
}

void Viewport2::cleanup_render_target()
{
	if (m_target.is_valid()) {
		ev2::destroy_render_target(Editor::ctx(), m_target);
	}
	m_target = EV2_NULL_HANDLE(RenderTarget);
}

int Viewport2::update(int *p_flags)
{
	ev2::GfxContext *ctx = Editor::ctx();

	if (m_needs_resize) {
		cleanup_render_target();
		imgui_texture = VK_NULL_HANDLE;

		if (m_size.x <= 0 || m_size.y <= 0) {
			return Editor::OK;
		}

		m_target = ev2::create_render_target(ctx, (uint32_t)m_size.x, (uint32_t)m_size.y,
						 m_target_flags);
		if (!m_target.is_valid()) {
			return Editor::ERROR;
		}

		VkImageView view = ev2::get_render_target_color_view(m_target);
		imgui_texture = ImGui_ImplVulkan_AddTexture(
			view,
			VK_IMAGE_LAYOUT_GENERAL
		);

		ev2::ImageID color_img = {};
		ev2::get_render_target_images(m_target, &color_img, nullptr);

		ev2::pre_destroy_callback(ctx, color_img,
		[tex = imgui_texture](){
			ImGui_ImplVulkan_RemoveTexture(tex);
		});

		if (p_flags)
			*p_flags |= RESIZED_BIT;
		m_needs_resize = false;
	}

	return Editor::OK;
}

Viewport2 &Viewport2::extend_settings(std::function<void()>&& callback)
{
	settings_callbacks.push_back(std::move(callback));
	return *this;
}

int Viewport2::imgui(int *p_flags)
{
	int update_flags = 0;
	int status = update(&update_flags);

	if (p_flags)
		*p_flags |= update_flags;

	if (status < Editor::OK)
		return status;

	if (ImGuiWindow* window = ImGui::FindWindowByName(m_name.c_str())) {
		bool isDraggingThisWindow = ImGui::GetCurrentContext()->MovingWindow == window;

		if (isDraggingThisWindow) {
			ImVec2 pos = window->Pos;

			ImVec2 clamped = pos;
			clamped.y = ImMax(pos.y, 0.0f);

			if (clamped.y != pos.y) {
				// Nudge the click offset so the cursor doesn't desync from the window
				ImGuiContext& g = *ImGui::GetCurrentContext();
				g.ActiveIdClickOffset.y += pos.y - clamped.y; 
				window->Pos = clamped;
			}
		}
	}

	ImGui::SetNextWindowPos(ImVec2((float)m_pos.x, (float)m_pos.y), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2((float)m_size.x, (float)m_size.y), ImGuiCond_FirstUseEver);

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

				if (std::shared_ptr<ICameraController> cam = m_cam.lock())
					cam->imgui();

				for (const std::function<void()> &callback : settings_callbacks)
		 			callback();
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
				ev2::ImageID color;
				ev2::get_render_target_images(m_target, &color, nullptr);
				ev2::cmd_use_image(Editor::gui_pass(), color, ev2::USAGE_SAMPLED_GRAPHICS);

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

	// Update the view after the ui has processed. Note that render target size
	// is updated on the frame after.
	const Editor::InputData &input = Editor::input();

	const bool is_active = is_content_selected(); 

	CameraInput cam_input = {
		.cursor = glm::vec2(input.mouse_pos[0]) - glm::vec2(m_pos),
		.cursor_delta = input.get_mouse_delta(),
		.size = get_size(), 
		.scroll_delta = (float)input.scroll_delta.y,
		.dt = (float)input.dt,
		.move_dir = input.move_dir,
		.active = is_active,
		.capture_mouse = is_active && input.mouse_mode == GLFW_CURSOR_DISABLED,
		.left_down = input.left_mouse_pressed,
		.right_down = input.right_mouse_pressed,
	};

	const bool is_empty = (m_size.x * m_size.y) == 0; 

	if (std::shared_ptr<ICameraController> camera = m_cam.lock(); camera && !is_empty) {
		if (!(update_flags & RESIZED_BIT) && is_content_selected())
			camera->update(cam_input);

		glm::mat4 view = camera->view();
		glm::mat4 proj = camera->proj(glm::vec2(get_size()));

		m_screen_to_world = glm::inverse(proj * view);

		ev2::update_view(Editor::ctx(), m_view, glm::value_ptr(view), glm::value_ptr(proj));
	}

	if (!open && p_flags)
		*p_flags |= SHOULD_CLOSE_BIT;

	return open ? Editor::OK : Editor::SHOULD_CLOSE;
}

void Viewport2::set_camera(std::shared_ptr<ICameraController> camera)
{
	m_cam = camera;
}

