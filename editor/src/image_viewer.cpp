#include "ev2/editor.h"
#include "ev2/panning_camera.h"
#include "ev2/image_viewer.h"

#include "ev2/utils/log.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

glm::vec2 ImageViewer2::get_grid_cursor_pos()
{
	glm::ivec2 viewport_size = Viewport::get_size();
	glm::ivec2 viewport_pos = Viewport::get_pos();
	glm::mat4 screen_to_world = Viewport::get_screen_to_world();

	glm::vec2 uv = (glm::vec2(Editor::input().mouse_pos[0]) -
		glm::vec2(viewport_pos.x, viewport_pos.y)) / 
		glm::vec2(viewport_size.x, viewport_size.y); 

	glm::uvec2 image_size;

	ev2::get_image_dims(Editor::ctx(), image, &image_size.x, &image_size.y, nullptr);

	uv = glm::vec2(uv.x, 1.f - uv.y);

	float aspect = float(image_size.x)/float(image_size.y);

	uv = screen_to_world * glm::vec4(2.f*uv - glm::vec2(1.f),0,1);
	uv = 0.5f * (uv + glm::vec2(aspect, 1.f));

	return glm::vec2(uv); 
}

ImageViewer2::ImageViewer2(
	uint32_t x, 
	uint32_t y, 
	uint32_t w, 
	uint32_t h, 
	uint32_t in_flags,
	const char *pipeline,
	const char *name) :
	Viewport(name, x, y, w, h)
{
	camera = std::make_shared<PanningCamera>();
	set_camera(camera);
	pipeline_path = pipeline;

	flags = in_flags;

	extend_settings([this, ctx = Editor::ctx()]{
		ImGui::BeginChild("FixedWidthWrapper", ImVec2(250, 0), ImGuiChildFlags_AutoResizeY);

		uint32_t max_levels = 0, max_layers = 0;
		uint32_t sel_level = level, sel_layer = layer;

		ev2::get_image_dims(ctx, this->image, nullptr, nullptr, &max_layers, &max_levels);

		if (ImGui::CollapsingHeader("Mip level selector")) {
			ImGui::Indent();
			for (uint32_t i = 0; i < max_levels; ++i) {
				char namebuf[100];
				snprintf(namebuf, sizeof(namebuf), "level_%u", i);

				if (ImGui::Selectable(namebuf, this->level == i)) {
					sel_level = i;
				}
			}
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Layer selector")) {
			ImGui::Indent();
			for (uint32_t i = 0; i < max_layers; ++i) {
				char namebuf[100];
				snprintf(namebuf, sizeof(namebuf), "layer_%u", i);

				if (ImGui::Selectable(namebuf, this->layer == i)) {
					sel_layer = i;
				}
			}
			ImGui::Unindent();
		}

		if (ImGui::CollapsingHeader("Filter")) {
			ImGui::Indent();
			if (ImGui::Selectable("Nearest", this->rd.filter == ev2::FILTER_NEAREST)) {
				set_texture_filter(ev2::FILTER_NEAREST);
			}
			if (ImGui::Selectable("Bilinear", this->rd.filter == ev2::FILTER_BILINEAR)) {
				set_texture_filter(ev2::FILTER_BILINEAR);
			}
			ImGui::Unindent();
		}

		if (sel_level != level || sel_layer != layer) {
			this->level = sel_level;
			this->layer = sel_layer;

			this->set_image(ctx, this->image, sel_level, sel_layer);
		}
		if (ImGui::CollapsingHeader("Pipeline", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Indent();

			ImGui::PushID(Viewport::get_id());

			char path[PATH_MAX] {};
			std::string text = "" + pipeline_path;

			ImGui::Text("%s",text.c_str());

			ImGuiInputTextFlags text_flags =
				ImGuiInputTextFlags_EnterReturnsTrue |
				ImGuiInputTextFlags_ElideLeft; 

			if (ImGui::InputTextWithHint("","Enter pipeline", path, sizeof(path), text_flags)) {
				if (this->pipeline_path.compare(path)) {
					if (set_pipeline(path) != ev2::SUCCESS) {
						log_warn("%s: Failed to set pipeline to %s", 
							this->get_name(),
							path
						);
					}
				}
			}
			ImGui::PopID();
			ImGui::Unindent();
		}
		ImGui::EndChild();
	});

}

ImageViewer2::~ImageViewer2()
{
	destroy(Editor::ctx());
}

void ImageViewer2::set_texture_filter(ev2::TextureFilter filter)
{
	ev2::GfxContext *ctx = Editor::ctx();

	if (filter != rd.filter) {
		ev2::destroy_texture(ctx, rd.tex);
		rd.tex = ev2::create_texture(ctx, image, filter, level, layer);
	}
	rd.filter = filter;
}

int ImageViewer2::set_pipeline(const char *path)
{
	ev2::GfxContext *ctx = Editor::ctx();

	ev2::GfxPipelineID pipeline = ev2::load_graphics_pipeline(ctx, path);

	if (rd.pipeline.is_valid() && rd.pipeline == pipeline)
		return 0;

	if (!EV2_VALID(pipeline))
		return Editor::ERROR;

	if (rd.bindings.is_valid()) {
		ev2::destroy_bindings(ctx, rd.bindings);
	}

	ev2::BindingsID bindings = ev2::create_bindings(
		ctx, pipeline, EV2_GFX_SET_PER_DRAW, ev2::BINDING_MODE_DYNAMIC);
	rd.pipeline = pipeline;
	rd.bindings = bindings;
	pipeline_path = path;

	return Editor::OK;
}

int ImageViewer2::update(ev2::GfxContext *ctx, int *p_flags)
{
	int status = Editor::OK;
	int update_flags = 0;

	if (status = Viewport::imgui(&update_flags); status < Editor::OK) {
		return status;
	}

	if (p_flags)
		*p_flags |= update_flags;

	// Nothing to bind for a panel that is going away this frame.
	if (update_flags & Viewport::SHOULD_CLOSE_BIT)
		return Editor::OK;

	ev2::Result res = ev2::reset_bindings(ctx, rd.bindings);
	if (res != ev2::SUCCESS)
		return Editor::ERROR;

	res = ev2::bind_texture(ctx, rd.bindings, "u_tex", rd.tex);
	if (res != ev2::SUCCESS)
		return Editor::ERROR;

	ev2::flush_bindings(ctx, rd.bindings);

	return Editor::OK;
}

int ImageViewer2::set_image(ev2::GfxContext *ctx,
	ev2::ImageID img, uint32_t lvl, uint32_t lyr)
{
	int  result = Editor::OK;

	if (!rd.pipeline.is_valid()) {
		result = set_pipeline(pipeline_path.c_str());
	}

	if (result)
		return result;

	this->image = img;
	if (rd.tex.is_valid())
		ev2::destroy_texture(ctx, rd.tex);

	rd.tex = ev2::create_texture(ctx, img, rd.filter, lvl, lyr);

	return rd.tex.is_valid() ? Editor::OK : Editor::ERROR;
}

void ImageViewer2::record_draw(ev2::PassID pass)
{
	ev2::cmd_use_image(pass, image, ev2::USAGE_SAMPLED_GRAPHICS);
	ev2::cmd_bind_gfx_pipeline(pass, rd.pipeline);
	ev2::cmd_bind_resources(pass, rd.bindings);
	ev2::cmd_custom(pass, [](VkCommandBuffer cmds){
		vkCmdDraw(cmds, 6, 1, 0, 0);
	});
}

void ImageViewer2::render(ev2::GfxContext *ctx)
{
	if (!image.is_valid()) {
		log_error("Image not initialized.");
		return;
	}

	if (!rd.bindings.is_valid()) {
		log_error("Bindings not initialized.");
		return;
	}

	ev2::GfxPassInfo pass_info = {
		.target = get_target(),
		.view = get_view(),
		.clear_color = true,
		.clear_depth = true,
		.name = get_name()
	};
	ev2::PassID pass = ev2::begin_gfx_pass(ctx, &pass_info);
	record_draw(pass);
	ev2::end_pass(ctx, pass);
}

void ImageViewer2::destroy(ev2::GfxContext *ctx)
{
	if (rd.bindings.is_valid())
		ev2::destroy_bindings(ctx, rd.bindings);
	if (rd.tex.is_valid())
		ev2::destroy_texture(ctx, rd.tex);
}

