#include "heightmap_viewer.h"

#include "ev2/editor.h"

#include <ev2/utils/log.h>

#include <cstring>
#include <vector>

static std::vector<uint32_t> create_quad_indices(uint32_t n)
{
	std::vector<uint32_t> indices;
	for (uint32_t i = 0; i < n; ++i) {
		for (uint32_t j = 0; j < n; ++j) {
			uint32_t in = std::min(i + 1,n - 1);
			uint32_t jn = std::min(j + 1,n - 1);

			indices.push_back((n) * i  + j);
			indices.push_back((n) * in + j);
			indices.push_back((n) * in + jn);

			indices.push_back((n) * i  + j);
			indices.push_back((n) * in + jn);
			indices.push_back((n) * i  + jn);
		}
	}

	return indices;
}

HeightmapViewer::HeightmapViewer(ev2::TextureID tex)
{
	m_viewport = std::make_unique<Viewport>("3D view", 700, 0, 500, 500,
		ev2::RENDER_TARGET_CREATE_DEPTH_BIT | ev2::RENDER_TARGET_CREATE_COLOR_BIT
	);

	m_camera = std::make_shared<MotionCamera>(
		glm::dvec3(0,0,0), glm::dvec3(1,1,1), glm::dvec3(0,0,1));

	m_camera->near_plane = 0.01f;
	m_camera->far_plane = 10.f;

	m_viewport->set_camera(m_camera);

	m_viewport->extend_settings([this](){
		ImGui::BeginChild("FixedWidthWrapper", ImVec2(250, 0), ImGuiChildFlags_AutoResizeY);
		ImGui::SliderFloat("Scale", &m_uniforms.scale, 0, 1.f);
		ImGui::EndChild();
	});

	//-----------------------------------------------------------------------------
	// Setup pipeline

	ev2::GfxContext *ctx = Editor::ctx();

	rd.pipeline = ev2::load_graphics_pipeline(ctx, "core://pipeline/heightmap.yaml");
	rd.bindings = ev2::create_bindings(
		ctx, rd.pipeline, EV2_GFX_SET_PER_DRAW, ev2::BINDING_MODE_STATIC);

	if (tex.is_valid() && set_texture(ctx, tex) != Editor::OK) {
		log_error("Failed to set texture");
	}
	return;
}

HeightmapViewer::~HeightmapViewer()
{
	destroy(Editor::ctx());
}

int HeightmapViewer::set_texture(ev2::GfxContext *ctx, ev2::TextureID tex)
{
	uint32_t h, w;
	ev2::get_texture_dims(ctx, tex, &w, &h, nullptr);

	//-----------------------------------------------------------------------------
	// Prepare index buffer
	if (rd.w != w || rd.h != h) {
		std::vector<uint32_t> indices = create_quad_indices(w);

		size_t indices_size = indices.size()*sizeof(uint32_t);

		if (rd.ibo.is_valid())
			ev2::destroy_buffer(ctx, rd.ibo);

		rd.ibo = ev2::create_buffer(ctx, indices_size, ev2::BUFFER_USAGE_INDEX_BUFFER_BIT);

		ev2::UploadContext uc = ev2::begin_upload(ctx, indices_size, alignof(uint32_t));
		memcpy(uc.ptr, indices.data(), indices_size);
		ev2::BufferUpload up = {.size = indices_size};
		ev2::commit_buffer_uploads(ctx, uc, rd.ibo, &up, 1);
	}

	if (ev2::bind_texture(ctx, rd.bindings, "u_tex", tex) != ev2::SUCCESS) {
		return Editor::ERROR;
	}
	ev2::flush_bindings(ctx, rd.bindings);

	rd.tex = tex;
	rd.w = w;
	rd.h = h;

	return Editor::OK;
}

int HeightmapViewer::update(ev2::GfxContext *ctx, int *p_flags)
{
	(void)ctx;
	return m_viewport->imgui(p_flags);
}

void HeightmapViewer::render(ev2::GfxContext *ctx)
{
	glm::ivec2 panel_size = m_viewport->get_size();

	if (panel_size.x * panel_size.y == 0)
		return;

	ev2::Rect rect = {
		.x0 = 0, .y0 = 0,
		.w = (uint32_t)panel_size.x, .h = (uint32_t)panel_size.y
	};

	ev2::GfxPassInfo pass_info = {
		.target = m_viewport->get_target(),
		.view = m_viewport->get_view(),
		.viewport = rect,
		.clear_color = true,
		.clear_depth = true,
		.name = m_viewport->get_name(),
	};

	ev2::PassID pass = ev2::begin_gfx_pass(ctx, &pass_info);
	ev2::cmd_use_buffer(pass, rd.ibo, ev2::USAGE_INDEX_INPUT);

	ev2::ImageID image = ev2::get_backing_image(ctx, rd.tex);
	ev2::cmd_use_image(pass, image, ev2::USAGE_SAMPLED_GRAPHICS);

	ev2::cmd_bind_gfx_pipeline(pass, rd.pipeline);
	ev2::cmd_push_constant(pass, rd.pipeline, 0, sizeof(Uniforms), &m_uniforms);

	ev2::cmd_bind_resources(pass, rd.bindings);
	ev2::cmd_bind_index_buffer(pass, rd.ibo, 0);

	uint32_t idx_count = 6 * rd.w * rd.h;

	ev2::cmd_custom(pass, [idx_count](VkCommandBuffer cmds){
		vkCmdDrawIndexed(cmds, idx_count, 1, 0, 0, 0);
	});

	ev2::end_pass(ctx, pass);
}

void HeightmapViewer::destroy(ev2::GfxContext *ctx)
{
	if (rd.bindings.is_valid())
		ev2::destroy_bindings(ctx, rd.bindings);
	if (rd.ibo.is_valid())
		ev2::destroy_buffer(ctx, rd.ibo);

	m_viewport.reset();
	m_camera.reset();
}
