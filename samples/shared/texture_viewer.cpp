#include "texture_viewer.h"
#include "ev2/utils/camera.h"

#include "ev2/utils/log.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

glm::vec2 ImageViewerPanel::get_world_cursor_pos()
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

ImageViewerPanel::ImageViewerPanel(App *app, 
	uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char *pipeline, const char *name) : 
	app(app)
{
	static uint32_t ctr = 0;

	panel_idx = ++ctr;

	if (!name) {
		std::string name_str = "Texture" + std::to_string(panel_idx);
		name = name_str.c_str();
	}

	panel.reset(new Panel(app, app->ctx, name, x, y, w, h));

	pipeline_path = pipeline;
}

int ImageViewerPanel::set_pipeline(const char *path)
{
	ev2::GfxPipelineID pipeline = ev2::load_graphics_pipeline(app->ctx, path);

	if (rd.pipeline.id == pipeline.id)
		return 0;

	if (!EV2_VALID(pipeline))
		return App::ERROR;

	ev2::BindingsID bindings = ev2::create_bindings(
		app->ctx, pipeline, EV2_GFX_SET_PER_DRAW, ev2::BINDING_MODE_STATIC);
	ev2::Result res = ev2::bind_texture(app->ctx, bindings, "u_tex", rd.tex);
	if (res != ev2::SUCCESS)
		return App::ERROR;

	ev2::flush_bindings(app->ctx, bindings);

	rd.pipeline = pipeline;
	rd.bindings = bindings;
	pipeline_path = path;

	return App::OK;
}

int ImageViewerPanel::init(ev2::GfxContext *ctx, ev2::ImageID image) 
{
	this->image = image;
	rd.camera = ev2::create_view(ctx, nullptr, nullptr);
	rd.tex = ev2::create_texture(ctx, image, ev2::FILTER_BILINEAR);

	int result = App::OK;

	if (!rd.camera.is_valid() || !rd.tex.is_valid()) {
		result = App::ERROR;
		goto error;
	}

	result = set_pipeline(pipeline_path.c_str());
	if (result)
		goto error;;

	panel->set_settings([this]{
		ImGui::BeginChild("FixedWidthWrapper", ImVec2(250, 0), ImGuiChildFlags_AutoResizeY); // fixed width, auto height
		if (ImGui::CollapsingHeader("Pipeline", ImGuiTreeNodeFlags_DefaultOpen)) {

			ImGui::PushID(this->panel_idx);

			char path[PATH_MAX] {};
			std::string text = "" + pipeline_path;

			ImGui::Text("%s",text.c_str());

			ImGuiInputTextFlags flags =
				ImGuiInputTextFlags_EnterReturnsTrue |
				ImGuiInputTextFlags_ElideLeft; 

			if (ImGui::InputTextWithHint("","Enter pipeline", path, sizeof(path), flags)) {
				if (this->pipeline_path.compare(path)) {
					if (set_pipeline(path) != ev2::SUCCESS) {
						log_warn("%s: Failed to set pipeline to %s", 
							this->panel->get_name(),
							path
						);
					}
				}
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
	});

	return result;
error:
	destroy(ctx);
	return result;
}

int ImageViewerPanel::update(ev2::GfxContext *ctx)
{
	if (!panel->imgui()) {
		return App::SHOULD_CLOSE;
	}

	glm::ivec2 panel_size = panel->get_size();
	glm::ivec2 panel_pos = panel->get_pos();

	if (panel->is_content_selected()) {

		float aspect = (float)panel_size.y/(float)panel_size.x;

		if (panel->is_hovered()) {
			rd.zoom *= pow(2, app->input.scroll_delta.y);
			rd.proj = camera_proj_2d(aspect, rd.zoom);
		}

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

ev2::PassID ImageViewerPanel::begin_pass(ev2::GfxContext *ctx)
{
	glm::ivec2 win_size = panel->get_size(); 

	ev2::RenderTargetID window_target = panel->get_target();
	ev2::Rect view_rect = {0,0, (uint32_t)win_size.x, (uint32_t)win_size.y};

	return ev2::begin_gfx_pass(ctx, window_target, rd.camera, view_rect);
}

void ImageViewerPanel::render(ev2::GfxContext *ctx)
{
	if (!image.is_valid())
		return;

	ev2::PassID pass = this->begin_pass(ctx);
	ev2::cmd_use_image(pass, image, ev2::USAGE_SAMPLED_GRAPHICS);
	ev2::cmd_bind_gfx_pipeline(pass, rd.pipeline);
	ev2::cmd_bind_resources(pass, rd.bindings);
	ev2::cmd_custom(pass, [](VkCommandBuffer cmds){
		vkCmdDraw(cmds, 6, 1, 0, 0);
	});
	ev2::end_pass(ctx, pass);
}

void ImageViewerPanel::destroy(ev2::GfxContext *ctx)
{
	if (rd.bindings.is_valid())
		ev2::destroy_bindings(ctx, rd.bindings);
	if (rd.tex.is_valid())
		ev2::destroy_texture(ctx, rd.tex);
	if (rd.camera.is_valid())
		ev2::destroy_view(ctx, rd.camera);
	panel.reset();
}

