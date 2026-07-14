
#include <ev2/imgui/inspector.h>
#include "inspector_impl.h"
#include "backends/vulkan/context_impl.h"

#include <vulkan/vk_enum_string_helper.h>

#ifdef EV2_ENABLE_IMGUI

#include <imgui.h>
#include "imgui_internal.h"

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
	if (!image->name) {
		sprintf(namebuf, "image_%d", id.slot);
	}

	if (ImGui::Selectable(image->name ? image->name : namebuf, isSelected)) {
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

constexpr ImVec2 ImVec2_Add(const ImVec2& a, const ImVec2 &b)
{
	return ImVec2(a.x + b.x, a.y + b.y);
}
constexpr ImVec2 ImVec2_Sub(const ImVec2& a, const ImVec2 &b)
{
	return ImVec2(a.x - b.x, a.y - b.y);
}

static void resource_state_text(const char *header, const ResourceStateFlags &state)
{
	std::string access = string_VkAccessFlags2(state.access);
	std::string stage = string_VkPipelineStageFlags2(state.stage);

	ImGui::Text(
		"%s:\n"
		"\tAccess: %s\n"
		"\tStage: %s",
		header,
		access.c_str(),
		stage.c_str()
	);
}

static void edge_tooltip(const RenderGraph *rg, const PassEdge *edge, const char *name)
{
	ImGui::BeginTooltip();
	ImGui::Text("%s", name);
	ImGui::Separator();
	resource_state_text("Producer", edge->src_state);
	resource_state_text("Consumer", edge->dst_state);
	ImGui::EndTooltip();
}

void render_graph_imgui(const RenderGraph *rg)
{

	if (!g_state.render_graph_window_open)
		return;

	uint32_t node_count = (uint32_t)rg->nodes.size(); 
	uint32_t edge_count = (uint32_t)rg->edges.size(); 
	std::vector<std::vector<uint32_t>> incoming(node_count);

	for (uint32_t e = 0; e < rg->edges.size(); ++e) {
		const PassEdge &edge = rg->edges[e];

		if (edge.dst_node == PASS_NODE_INDEX_OUT_OF_FRAME)
			continue;
		incoming[edge.dst_node].push_back(e);
	}

	std::vector<uint32_t> depths(node_count, 0);

	uint32_t longest_chain = 0;

	for (uint32_t n = 0; n < node_count; ++n) {
		if (incoming[n].empty())
			continue;

		uint32_t max_depth = 0;
		for (uint32_t e : incoming[n]) {
			const PassEdge &edge = rg->edges[e];
			if (edge.src_node != PASS_NODE_INDEX_OUT_OF_FRAME)
				max_depth = std::max(max_depth, depths[edge.src_node]);
		}
		uint32_t depth = 1 + max_depth; 
		depths[n] = depth;
		longest_chain = std::max(depth, longest_chain);
	}

	std::vector<std::vector<uint32_t>> levels (1 + longest_chain);

	for (uint32_t n = 0; n < node_count; ++n) {
		levels[depths[n]].push_back(n);
	}

	struct NodeDrawData {
		ImVec2 pos;
		ImVec2 size;
		const char *name;
	};

	struct EdgeDrawData {
		ImVec2 src_pos;
		ImVec2 dst_pos;
		TaggedResource resource;
	};

	std::vector<NodeDrawData> node_data (node_count);
	std::vector<EdgeDrawData> edge_data (edge_count);

	constexpr float node_x_spacing = 300.f;
	constexpr float node_y_spacing = 125.f;

	float x_level = 0;
	for (const std::vector<uint32_t> &level : levels) {
		float h = 0;

		for (uint32_t n : level) {
			const PassNode &node = rg->nodes[n];

			ImVec2 size (100, 100);

			NodeDrawData data{
				.pos = ImVec2_Sub(
					ImVec2(x_level, h),
					ImVec2(0.5f*size.x, 0.5f*size.y)
				),
				.size = size,
				.name = node.name.c_str(),
			};

			node_data[n] = data;

			uint32_t in_count = (uint32_t)incoming[n].size();

			float dy = size.y/(float)in_count;
			float y = data.pos.y + 0.5f*dy;

			for (uint32_t e : incoming[n]) {
				edge_data[e].dst_pos = ImVec2(
					data.pos.x,
					y
				);

				y += dy;
			}

			h += node_y_spacing;
		}

		x_level += node_x_spacing;
	}

	for (uint32_t e = 0; e < edge_count; ++e) {
		const PassEdge &edge = rg->edges[e];
		
		const NodeDrawData *src_node = edge.src_node == PASS_NODE_INDEX_OUT_OF_FRAME ?
			nullptr : &node_data[edge.src_node];
		const NodeDrawData *dst_node = edge.dst_node == PASS_NODE_INDEX_OUT_OF_FRAME ?
			nullptr : &node_data[edge.dst_node];

		if (src_node && dst_node) {
			edge_data[e].src_pos = ImVec2_Add(
				src_node->pos,
				ImVec2(src_node->size.x, 0.5f*src_node->size.y)
			); 		
		} else if (dst_node) {
			ImVec2 dst_pos = edge_data[e].dst_pos;
			edge_data[e].src_pos = ImVec2(
				dst_pos.x - dst_node->size.x - 0.025f*node_x_spacing,
				dst_pos.y
			); 		
		}	

		edge_data[e].resource = edge.resource;
	}

	static ImVec2 pan = ImVec2(0,0);
	const ImGuiIO &io = ImGui::GetIO();

	static float scale = 1.f;

	if (ImGui::Begin("RenderGraph", &g_state.render_graph_window_open,
				  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)
	) {

		ImVec2 origin = ImGui::GetCursorScreenPos();
		ImVec2 content = ImGui::GetContentRegionAvail();

		if (ImGui::IsWindowHovered() && ImGui::GetMousePos().y >= origin.y) {
			ImGui::GetCurrentWindow()->Flags |= ImGuiWindowFlags_NoMove;

			scale *= powf(1.5f, io.MouseWheel);
			
			if (io.MouseDown[0]) {
				pan.x -= io.MouseDelta.x/scale;
				pan.y -= io.MouseDelta.y/scale;
			}
		} 

		auto to_screen = [=](ImVec2 graph_pos) -> ImVec2 {
			return ImVec2(
				origin.x + 0.5f*content.x + (graph_pos.x  - pan.x) * scale,
				origin.y + 0.5f*content.y + (graph_pos.y  - pan.y) * scale
			);
		};

		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->ChannelsSplit(2); // 0 = links, 1 = nodes
		
		constexpr uint32_t node_color = 0xAA333333;
		constexpr uint32_t border_color = 0xBB000000;
		constexpr uint32_t selected_border_color = 0xFFFFFFFF;
		constexpr uint32_t text_color = 0xFFFFFFFF;
		constexpr uint32_t edge_color = 0xFFCCCCCC;

		for (const NodeDrawData &node : node_data) {

			dl->ChannelsSetCurrent(1);
			ImVec2 p_min = to_screen(node.pos);
			ImVec2 p_max = to_screen(ImVec2_Add(node.pos, node.size));
			dl->AddRectFilled(p_min, p_max, node_color, 4.0f);
			dl->AddRect(p_min, p_max, border_color, 4.0f);
			dl->AddText(ImVec2_Add(p_min , ImVec2(6,6)), text_color, node.name);
		}

		std::string name;

		constexpr uint32_t selected_bg_color = 0xDD663333;
		constexpr uint32_t hover_bg_color = 0xDD994444;
		constexpr uint32_t resource_bg_color = 0xDD000000;

		bool has_hovered_edge = false;
		
		for (uint32_t e = 0; e < edge_count; ++e) {
			if (rg->edges[e].dst_node == PASS_NODE_INDEX_OUT_OF_FRAME)
				continue;

			const EdgeDrawData &edge = edge_data[e];

			dl->ChannelsSetCurrent(0);
			ImVec2 p1 = to_screen(edge.src_pos);
			ImVec2 p2 = to_screen(edge.dst_pos);
			float dx = std::max(50.0f, fabsf(p2.x - p1.x) * 0.5f);
			dl->AddBezierCubic(p1, 
				ImVec2_Add(p1 , ImVec2(dx,0)), 
				ImVec2_Sub(p2 , ImVec2(dx,0)), 
				p2, 
				edge_color, 
				2.0f
			);

			name += edge.resource.type_str() + std::to_string(edge.resource.id());

			ImVec2 mid = ImVec2(0.5f*(p1.x + p2.x), 0.5f*(p1.y + p2.y));
			ImVec2 text_size = ImGui::CalcTextSize(name.c_str());
			ImVec2 pad(4.0f, 2.0f);
			ImVec2 box_min = ImVec2_Sub(mid, ImVec2_Add(ImVec2(text_size.x * 0.5f, text_size.y * 0.5f), pad));
			ImVec2 box_max = ImVec2_Add(mid, ImVec2_Add(ImVec2(text_size.x * 0.5f, text_size.y * 0.5f), pad));

			dl->ChannelsSetCurrent(1);

			// hit-test / selection
			ImGui::SetCursorScreenPos(box_min);
			ImGui::PushID((int)e);
			ImGui::InvisibleButton("resource_box", ImVec2_Sub(box_max, box_min));
			bool hovered = ImGui::IsItemHovered();
			bool clicked = ImGui::IsItemClicked();
			bool double_clicked = ImGui::IsMouseDoubleClicked(0);
			ImGui::PopID();

			if (clicked)
				g_state.selected_edge = e;
			if (hovered) {
				g_state.hovered_edge = e;
				g_state.hovered_edge_name = name;
				has_hovered_edge = true;

				if (hovered) {
					edge_tooltip(rg, &rg->edges[e], name.c_str());
				}
			}

			bool name_matches = name == g_state.hovered_edge_name;

			bool is_selected = (g_state.selected_edge == e);
			ImU32 box_bg = is_selected ? selected_bg_color
						 : (hovered || name_matches) ? hover_bg_color
										: resource_bg_color;

			dl->AddRectFilled(box_min, box_max, box_bg, 3.0f);
			dl->AddRect(box_min, box_max, is_selected ? selected_border_color : border_color, 3.0f);
			dl->AddText(ImVec2_Add(box_min, pad), text_color, name.c_str());

			name.clear();
		}

		dl->ChannelsMerge();

		if (!has_hovered_edge) {
			g_state.hovered_edge = UINT32_MAX;
			g_state.hovered_edge_name = "";
		}
	}

	//if (ImGui::BeginChild("Images", ImVec2(0, 300), true)) {
	//}
	//ImGui::EndChild();
	ImGui::End(); 

}

void inspector_panel_imgui(GfxContext *ctx)
{
	const FrameContext *prev_frame = ctx->frame_counter ? 
		ctx->get_frame(ctx->frame_counter - 1) : nullptr;

	if (ImGui::Begin(INSPECTOR_PANEL_NAME)) {
		if (prev_frame) {
			ev2::imgui::post_frame_submission_stats(
				prev_frame->render_graph->submissions.data(), 
				(uint32_t)prev_frame->render_graph->submissions.size()
			);
		}
		image_list_imgui(ctx);
		buffer_list_imgui(ctx);

		if (ImGui::RadioButton("Show render graph", g_state.render_graph_window_open)) {
			g_state.render_graph_window_open = !g_state.render_graph_window_open;
		}
	}
	ImGui::End();

	if (prev_frame) {
		render_graph_imgui(prev_frame->render_graph.get());
	}
}

void editor_panel_imgui(GfxContext *ctx)
{
	if (ImGui::Begin(EDITOR_PANEL_NAME)) {
		ImGui::SliderInt("Target framerate", &ctx->framerate_hz, 10, 240);
	}
	ImGui::End();
}

}

#endif
