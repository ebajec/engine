
#include <ev2/imgui/inspector.h>
#include <vulkan/vk_enum_string_helper.h>

#ifdef EV2_ENABLE_IMGUI

#include <imgui.h>

#include "backends/vulkan/context_impl.h"

namespace ev2 {

struct ImageCallback {
	void *usr;
	void (*callback)(void *, ImageID);
};

static struct InspectorPanelState
{
	std::vector<ev2::ImageID> selected_images;

	PoolID selected;

	ImageCallback image_viewer_open;
	ImageCallback image_viewer_close;
} g_state;

static void image_entry_imgui(PoolID id, const Image *image)
{
	bool isSelected = (g_state.selected == id);

	char namebuf[100];
	sprintf(namebuf, "image_%d", id.slot);

	if (ImGui::Selectable(namebuf, isSelected)) {
		g_state.selected = id;
	}
	if (ImGui::BeginPopupContextItem()) {
		if (ImGui::MenuItem("Open viewer")) {
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Close viewer")) {
			// ...
		}
		ImGui::EndPopup();
	}
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
	}

	bool is_selected =  g_state.selected == id;

	if (is_selected) {
        ImGui::Indent();
		ImGui::Text("width: %d\n", image->w);
		ImGui::Text("height: %d\n", image->h);
		ImGui::Text("depth: %d\n", image->d);
		ImGui::Text("levels: %d\n", image->levels);
		ImGui::Text("format: %s\n", string_VkFormat(image->format));
        ImGui::Unindent();
	}

}

static void image_list_imgui(GfxContext *ctx)
{
	typedef decltype(GfxContext::image_pool)::element_type PoolType;
	constexpr uint32_t PageSize = 
		decltype(GfxContext::image_pool)::element_type::PageSizeValue;

	if (ImGui::BeginChild("Images", ImVec2(0, 300), true)) {

		size_t cap = ctx->image_pool->cap;

		for (size_t i = 0; i < ctx->image_pool->pages.size(); ++i) {
			const PoolType::Page *page = ctx->image_pool->pages[i].get();
			for (uint32_t j = 0; j < PageSize; ++j) {
				uint32_t idx = (uint32_t)(i * PageSize + j);
				if (idx >= ctx->image_pool->cap)
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
	ImGui::EndChild();
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
	}
	ImGui::End();
}

}

#endif
