
#include <ev2/imgui/inspector.h>
#include "inspector_impl.h"

#include <vulkan/vk_enum_string_helper.h>

#ifdef EV2_ENABLE_IMGUI

#include <imgui.h>

#include "backends/vulkan/context_impl.h"

namespace ev2::imgui {

InspectorPanelState g_state;

void on_destroy_image(ev2::ImageID image)
{
	if (g_vk.allow_resource_inspection) {
		g_state.image_viewer_close.exec(image);
	}
}

static void state_info_imgui(const ResourceState *state)
{
	ImGui::Text("Read syncs: %d\n", (uint32_t)state->read_syncs.size());
	ImGui::Text("write sync: %llx\n", (unsigned long long)state->write_sync.semaphore);
}

static void image_entry_imgui(PoolID id, const Image *image)
{
	bool isSelected = (g_state.selected == id);

	char namebuf[100];
	sprintf(namebuf, "image_%d", id.slot);

	if (ImGui::Selectable(namebuf, isSelected)) {
		g_state.selected = id;
	}

	ImageID img_id = ImageID{
		.id = id.slot,
		.gen = id.gen
	};

	if (ImGui::BeginPopupContextItem()) {
		if (ImGui::MenuItem("Open viewer")) {
			if (g_state.image_viewer_open.callback)
				g_state.image_viewer_open.exec(img_id);
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Close viewer")) {
			if (g_state.image_viewer_close.callback)
				g_state.image_viewer_close.exec(img_id);
		}
		ImGui::EndPopup();
	}
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
		if (g_state.image_viewer_open.callback)
			g_state.image_viewer_open.callback(g_state.image_viewer_open.usr, img_id);
	}

	bool is_selected =  g_state.selected == id;

	if (is_selected) {
        ImGui::Indent();
		ImGui::Text("width: %d\n", image->w);
		ImGui::Text("height: %d\n", image->h);
		ImGui::Text("depth: %d\n", image->d);
		ImGui::Text("levels: %d\n", image->levels);
		ImGui::Text("format: %s\n", string_VkFormat(image->format));
		state_info_imgui(&image->state);
        ImGui::Unindent();
	}
}

static void buffer_entry_imgui(PoolID id, const Buffer *buffer)
{
	bool isSelected = (g_state.selected == id);

	char namebuf[100];
	sprintf(namebuf, "buffer_%d", id.slot);

	if (ImGui::Selectable(namebuf, isSelected)) {
		g_state.selected = id;
	}

	bool is_selected =  g_state.selected == id;

	if (is_selected) {
        ImGui::Indent();
		ImGui::Text("size: %lld\n", (unsigned long long)buffer->size);
		state_info_imgui(&buffer->state);
        ImGui::Unindent();
	}
}

static void buffer_list_imgui(GfxContext *ctx)
{
	typedef decltype(GfxContext::buffer_pool)::element_type PoolType;
	constexpr uint32_t PageSize = 
		decltype(GfxContext::buffer_pool)::element_type::PageSizeValue;

	if (ImGui::CollapsingHeader("Buffers")) {

		size_t cap = ctx->buffer_pool->cap;

		for (size_t i = 0; i < ctx->buffer_pool->pages.size(); ++i) {
			const PoolType::Page *page = ctx->buffer_pool->pages[i].get();
			for (uint32_t j = 0; j < PageSize; ++j) {
				uint32_t idx = (uint32_t)(i * PageSize + j);
				if (idx >= cap)
					break;

				if (!page->in_use(j))
					continue;

				PoolID id = {
					.slot = 1 + idx,
					.gen = page->generation[j]
				};

				buffer_entry_imgui(id, &page->values[j]);
			}
		}

	}
	//ImGui::EndChild();
}

static void image_list_imgui(GfxContext *ctx)
{
	typedef decltype(GfxContext::image_pool)::element_type PoolType;
	constexpr uint32_t PageSize = 
		decltype(GfxContext::image_pool)::element_type::PageSizeValue;

	//if (ImGui::BeginChild("Images", ImVec2(0, 300), true)) {
	if (ImGui::CollapsingHeader("Images")) {

		size_t cap = ctx->image_pool->cap;

		for (size_t i = 0; i < ctx->image_pool->pages.size(); ++i) {
			const PoolType::Page *page = ctx->image_pool->pages[i].get();
			for (uint32_t j = 0; j < PageSize; ++j) {
				uint32_t idx = (uint32_t)(i * PageSize + j);
				if (idx >= cap)
					break;

				if (!page->in_use(j))
					continue;

				PoolID id = {
					.slot = 1 + idx,
					.gen = page->generation[j]
				};

				image_entry_imgui(id, &page->values[j]);
			}
		}

	}
	//ImGui::EndChild();
}

void post_frame_submission_stats(const RenderGraphSubmission *submissions, uint32_t count)
{
	char buf[100];

	if (ImGui::Begin(INSPECTOR_PANEL_NAME)) {

		if (ImGui::CollapsingHeader("Submissions")) {
			for (uint32_t i = 0; i < count; ++i) {
				sprintf(buf, "Submission %d", i);

				const RenderGraphSubmission &submission = submissions[i];

				ImGui::Text("%s",buf);
				ImGui::Text("\tWait   : %d", (uint32_t)submission.wait.size());
				ImGui::Text("\tSignal : %d", (uint32_t)submission.signal.size());
			}
		}
	}
	ImGui::End();
}

void set_image_viewer_open_callback(void* usr, void (*callback)(void *, ev2::ImageID))
{
	g_state.image_viewer_open = {
		.usr = usr,
		.callback = callback
	};
}

void set_image_viewer_close_callback(void* usr, void (*callback)(void *, ev2::ImageID))
{
	g_state.image_viewer_close = {
		.usr = usr,
		.callback = callback
	};
}

void inspector_panel_imgui(GfxContext *ctx)
{
	if (ImGui::Begin(INSPECTOR_PANEL_NAME)) {
		image_list_imgui(ctx);
		buffer_list_imgui(ctx);
	}
	ImGui::End();
}

}

#endif
