#include "texture_viewer.h"
#include "ev2/utils/camera.h"

#include "ev2/utils/log.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

glm::vec2 TextureViewerPanel::get_world_cursor_pos()
{
	glm::ivec2 panel_size = panel->get_size();
	glm::ivec2 panel_pos = panel->get_pos();
	glm::mat4 screen_to_world = glm::inverse(rd.proj*rd.view);

	glm::vec2 uv = (glm::vec2(app->input.mouse_pos[0]) -
		glm::vec2(panel_pos.x, panel_pos.y)) / 
		glm::vec2(panel_size.x, panel_size.y); 

	uv = glm::vec2(uv.x, 1.f - uv.y);

	uv = screen_to_world * glm::vec4(2.f*uv - glm::vec2(1.f),0,1);
	uv = 0.5f * (uv + glm::vec2(1.f));

	return glm::vec2(uv); 
}

TextureViewerPanel::TextureViewerPanel(App *app,
									   uint32_t x, uint32_t y, uint32_t w, uint32_t h,
									   const char *pipeline) : 
	app(app)
{
	static uint32_t ctr = 0;

	panel_idx = ++ctr;

	std::string name = "Texture" + std::to_string(panel_idx);
	panel.reset(new Panel(app, app->ctx, name.c_str(), x, y, w, h));


	pipeline_path = pipeline;
}

int TextureViewerPanel::update_pipeline(const char *path)
{
	ev2::GfxPipelineID pipeline = ev2::load_graphics_pipeline(app->ctx, path);

	if (!EV2_VALID(pipeline))
		return EXIT_FAILURE;

	ev2::ShaderBindingsID bindings = ev2::create_bindings(
		app->ctx, pipeline, EV2_GFX_SET_PER_DRAW, ev2::BINDING_MODE_STATIC);
	ev2::Result res = ev2::bind_texture(app->ctx, bindings, "u_tex", rd.tex);

	if (res != ev2::SUCCESS)
		return EXIT_FAILURE;

	ev2::flush_bindings(app->ctx, bindings);

	rd.pipeline = pipeline;
	rd.bindings = bindings;
	pipeline_path = path;

	return EXIT_SUCCESS;
}

int TextureViewerPanel::init(ev2::GfxContext *ctx, ev2::TextureID tex) 
{
	rd.camera = ev2::create_view(ctx, nullptr, nullptr);
	rd.tex = tex;

	int result = update_pipeline(pipeline_path.c_str());

	if (result)
		return result;

	if (result)
		return result;

	return 0;
}

int TextureViewerPanel::set_texture(ev2::GfxContext *ctx, ev2::TextureID tex)
{
	ev2::Result res = ev2::bind_texture(ctx, rd.bindings, "u_tex", tex);

	if (res)
		return EXIT_FAILURE;

	rd.tex = tex;

	return EXIT_SUCCESS;
}

int TextureViewerPanel::update(ev2::GfxContext *ctx)
{
	ImGui::Begin("Editor");
	ImGui::PushID(panel_idx);
	if (ImGui::CollapsingHeader(panel->get_name().c_str())) {
		char path[PATH_MAX] {};
		std::string text = "" + pipeline_path;

		ImGui::Text("current:\n%s",text.c_str());

		ImGuiInputTextFlags flags =
			ImGuiInputTextFlags_EnterReturnsTrue |
			ImGuiInputTextFlags_ElideLeft; 

		if (ImGui::InputTextWithHint("","Enter pipeline", path, sizeof(path), flags)) {
			if (pipeline_path.compare(path)) {
				if (update_pipeline(path) != ev2::SUCCESS) {
					log_warn("%s: Failed to set pipeline to %s", 
						panel->get_name().c_str(),
						path
					);
				}
			}
		}
	}
	ImGui::PopID();
	ImGui::End();

	panel->imgui(); 

	glm::ivec2 panel_size = panel->get_size();
	glm::ivec2 panel_pos = panel->get_pos();

	if (panel->is_content_selected()) {

		float aspect = (float)panel_size.y/(float)panel_size.x;
		rd.zoom *= pow(2, app->input.scroll_delta.y);

		rd.proj = camera_proj_2d(aspect, rd.zoom);

		glm::mat4 p_inv = glm::inverse(rd.proj);

		if (app->input.left_mouse_pressed && panel->is_content_selected()) {
			glm::dvec2 delta = app->input.get_mouse_delta()/(double)panel->get_size().x; 
			rd.center += 2.f*glm::vec2(glm::vec4(delta.x, -delta.y,0,0)/(aspect*rd.zoom));
		}

		rd.view[3] = glm::vec4(glm::inverse(glm::mat2(rd.view))*rd.center,0,1);
		ev2::update_view(ctx, rd.camera, glm::value_ptr(rd.view), glm::value_ptr(rd.proj));
	}
	return EXIT_SUCCESS;
}

ev2::PassID TextureViewerPanel::begin_pass(ev2::GfxContext *ctx)
{
	glm::ivec2 win_size = panel->get_size(); 

	ev2::RenderTargetID window_target = panel->get_target();
	ev2::Rect view_rect = {0,0, (uint32_t)win_size.x, (uint32_t)win_size.y};

	return ev2::begin_gfx_pass(ctx, window_target, rd.camera, view_rect);
}

void TextureViewerPanel::render(ev2::GfxContext *ctx)
{
	ev2::PassID pass = this->begin_pass(ctx);
	ev2::cmd_bind_gfx_pipeline(pass, rd.pipeline);
	ev2::cmd_bind_resources(pass, rd.bindings);
	ev2::cmd_custom(pass, [](VkCommandBuffer cmds){
		vkCmdDraw(cmds, 6, 1, 0, 0);
	});
	ev2::end_pass(ctx, pass);
}

void TextureViewerPanel::destroy(ev2::GfxContext *ctx)
{
	ev2::destroy_bindings(ctx, rd.bindings);
	panel.reset();
}

