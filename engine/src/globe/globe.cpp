#include "engine/globe/globe.h"
#include "engine/globe/tiling.h"

#include "renderer/opengl.h"
#include "renderer/gl_debug.h"

#include "resource/buffer.h"

#include "utils/camera.h"
#include "utils/geometry.h"
#include "utils/thread_pool.h"
#include "utils/geometry.h"

#include "globe/gpu_cache.h"

#include "terrain.h"

#include <debug/box_debug_view.h>
#include <debug/camera_debug_view.h>

#include <imgui.h>
#include <implot.h>

#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat3x2.hpp>

#include <vector>

#include <numeric>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <vector>
#include <complex.h>

#ifndef PI 
#define PI 3.14159265359
#endif

#ifndef QUATPI 
#define QUATPI 0.785398163397
#endif

static constexpr uint32_t TILE_VERT_WIDTH = 64;
static constexpr uint32_t TILE_VERT_COUNT = 
	TILE_VERT_WIDTH*TILE_VERT_WIDTH;

static constexpr double tile_scale_factor = 12;

struct DebugInfo
{
	std::unique_ptr<CameraDebugView> camera;
	std::unique_ptr<BoxDebugView> boxes;
	bool enable_boxes;
	bool fix_camera;
	int zoom;
};

struct alignas(8) GlobeVertex
{
	glm::vec3 pos;
	glm::vec2 uv;
	glm::vec3 normal;
	//glm::vec2 big_uv;
};

struct alignas(16) TileMetadata
{
	glm::vec4 coord;

	alignas(8) glm::vec2 tex_uv[2];
	alignas(8) glm::vec2 globe_uv[2];

	TileGPUIndex tex_idx;
	uint32_t code_lower;
	uint32_t code_upper;
};


template<
	typename ArgIt, 
	typename OutIt, 
	typename OpEqual,
	typename OpUnequal
>
OutIt set_difference2(ArgIt a_start, ArgIt a_end, ArgIt b_start, ArgIt b_end, OutIt out,
					  OpEqual op_equal, OpUnequal op_unequal)
{
	for (; a_start < a_end; ++a_start) {
		while (b_start < b_end && *b_start < *a_start)
			++b_start;

		if (b_start >= b_end || *a_start < *b_start) {
			op_unequal(*a_start);
			*(out++) = *a_start;
		} else { // when a[i] == b[i]
			op_equal(*a_start, *b_start);
		}
	}
	return out;
}

struct TileAllocator
{
	struct kv_t {
		uint64_t code;
		size_t idx;

		bool operator == (const kv_t &other) const {
			return code == other.code;
		}
		bool operator < (const kv_t &other) const {
			return code < other.code;
		}
	};

	std::vector<size_t> free_list;

	std::vector<kv_t> new_tiles;
	std::vector<kv_t> current;
	std::vector<kv_t> previous;

	static TileAllocator *create(size_t cap)
	{
		TileAllocator *alloc = new TileAllocator{};

		alloc->free_list.resize(cap);
		std::iota(alloc->free_list.begin(), alloc->free_list.end(), 0);

		return alloc;
	}

	size_t get_idx(uint64_t code)
	{
		auto it = std::lower_bound(current.begin(), current.end(), 
							 kv_t{.code = code, .idx = 0});

		if (it != current.end()) {
			return it->idx;
		}

		return SIZE_MAX;
	}

	void get_new(const kv_t **keys, size_t *count) const {
		*keys = new_tiles.data();
		*count = new_tiles.size();
	}

	// @brief Replaces old key-value pairs with new ones, allocating slots
	// for any new keys from stale entries
	void set(const std::vector<uint64_t> &keys)
	{
		size_t cap = free_list.size() + current.size();

		std::swap(current, previous);

		current.resize(keys.size());

		for (size_t i = 0; i < keys.size(); ++i) {
			current[i].code = keys[i];
		}

		std::sort(current.begin(), current.end());

		current.resize(std::min(keys.size(), cap));

		new_tiles.resize(std::max(previous.size(), current.size()));

		// stale tiles (previous \ current)
		// for tiles that are the same copy the values over
		auto iter = set_difference2(
			previous.begin(), previous.end(), 
			current.begin(), current.end(), 
			new_tiles.begin(),
			[](kv_t a, kv_t b){},		
			[](kv_t a){}
		);

		for (auto it = new_tiles.begin(); it < iter; ++it) {
			free_list.push_back(it->idx);
		}

		std::vector<size_t> &tmp = free_list;

		// newly added (current \ previous)
		iter = set_difference2(
			current.begin(), current.end(), 
			previous.begin(), previous.end(), 
			new_tiles.begin(),
			[](kv_t &a, kv_t b){
				a.idx = b.idx;
			},
			[&tmp](kv_t &a){
				size_t idx = tmp.back();
				tmp.pop_back();

				a.idx = idx;
			}
		);

		size_t count = (size_t)(iter - new_tiles.begin());
		new_tiles.resize(count);
	}
};

struct RenderData
{
	BufferID vbo;
	BufferID indirect;
	BufferID ibo;

	// texture array indices
	BufferID ssbo;

	MaterialID material;
	GLuint vao;

	std::vector<DrawCommand> cmds;
};

struct Globe
{
	ResourceTable *rt;

	GlobeStats stats;
	DebugInfo dbg;

	std::vector<uint64_t> selected_tiles;

	std::unique_ptr<TileAllocator> tile_allocator;

	RenderData render_data;

	std::unique_ptr<GPUTileCache> gpu_cache;
	std::unique_ptr<CPUTileCache> cpu_cache;
};

struct select_tiles_params
{
	const CPUTileCache * cpu_cache;
	const DebugInfo *debug;

	size_t max_tiles;
	double cull_radius;
	frustum_t frust;
	aabb3_t frust_box;
	glm::dvec3 origin;
	double res;
};

static void globe_init_debug(Globe *globe) {
	globe->dbg.boxes.reset(new BoxDebugView(globe->rt));
	globe->dbg.camera.reset(new CameraDebugView(globe->rt));
	globe->dbg.enable_boxes = false;
}

static void globe_draw_debug(Globe const *globe, RenderContext const & ctx)
{
	if (globe->dbg.enable_boxes) {
		ctx.bind_material(globe->dbg.boxes->material);
		ctx.draw_cmd_mesh_outline(globe->dbg.boxes->model);
	}

	if (globe->dbg.fix_camera)
		globe->dbg.camera->render(ctx);
}

static aabb3_t tile_aabb(TileCode code, float min, float max) 
{
	uint8_t face = code.face;

	aabb2_t rect = morton_u64_to_rect_f64(code.idx,code.zoom);
	glm::dvec2 mid_uv = 0.5*(rect.ur() + rect.ll());

	double s_min = 1.0 + (double)min;
	double s_max = 1.0 + (double)max;

	glm::dvec3 c[4] = {
		cube_to_globe(face, rect.ll()),
		cube_to_globe(face, rect.lr()),
		cube_to_globe(face, rect.ul()),
		cube_to_globe(face, rect.ur()),
	};
	glm::dvec3 mid = cube_to_globe(face, mid_uv);
	glm::dvec3 m[2] = {
		mid*s_max, mid*s_min
	};
	aabb3_t box = aabb3_bounding(m, 2);

	for (size_t i = 0; i < sizeof(c)/sizeof(c[0]); ++i) {
		aabb3_add(box, s_max*c[i]);
		aabb3_add(box, s_min*c[i]);
	}

	if (code.zoom == 0) {
		aabb3_add(box, cube_to_globe(face, 0.5*(rect.ll() + rect.lr())));
		aabb3_add(box, cube_to_globe(face, 0.5*(rect.ll() + rect.ul())));
		aabb3_add(box, cube_to_globe(face, 0.5*(rect.ur() + rect.ul())));
		aabb3_add(box, cube_to_globe(face, 0.5*(rect.ur() + rect.lr())));
	}

	return box;
}

static obb_t tile_obb(TileCode code, double min, double max) 
{
	uint8_t face = code.face;

	aabb2_t rect = morton_u64_to_rect_f64(code.idx,code.zoom);
	glm::dvec2 mid_uv = 0.5*(rect.ur() + rect.ll());

	double h = max - min;

	glm::dvec3 mid = cube_to_globe(face, mid_uv);
	mid *= (1.0 + 0.5*(min + max));

	double s_min = 1.0 + min;
	double s_max = 1.0 + max;

	obb_t box;
	box.T = orthonormal_globe_frame(mid_uv, face);
	box.O = mid;
	box.S = glm::dvec3(0,0,0.5*h);

	// TODO : It should be possible to determine the minimum 
	// dimensions of the box with less operations.
	glm::dvec3 c[8] = {
		cube_to_globe(face, rect.ll()),
		cube_to_globe(face, rect.lr()),
		cube_to_globe(face, rect.ul()),
		cube_to_globe(face, rect.ur()),
	};

	for (size_t i = 0; i < 4; ++i) {
		c[4 + i] = c[i] * s_max;
		c[i] *= s_min;
	}

	obb_add(box, sizeof(c)/sizeof(c[0]),c);

	return box;
}

struct selection_entry_t
{
	TileCode code;
	double dist;
};

static inline int select_tiles_rec(
	std::vector<selection_entry_t> &out, 
	const select_tiles_params *params,
	TileCode code)
{
	BoxDebugView * boxes = params->debug->enable_boxes ? 
		params->debug->boxes.get() : nullptr;

	if (code.zoom > 23)
		return 0;

	uint64_t u64 = tile_code_pack(code);

	mmt_result_t mmt_res = mmt_minmax(params->cpu_cache->mmt, u64);
	obb_t box = tile_obb(code, (double)mmt_res.min, (double)mmt_res.max);

	if (code.zoom > 1 && dot(box.T[2],params->origin) < 0)
		return 0;

	for (uint8_t i = 0; i < 6; ++i) {
		if (classify(box, params->frust.planes[i]) > 0) { 
			return 0;
		}
	}

	double d_min_sq = obb_dist_sq(box, params->origin);

	d_min_sq = std::max(tile_scale_factor*sqrt(d_min_sq),1e-6);

	double area = tile_factor(code.zoom);

	if (area/d_min_sq < params->res 
		|| mmt_res.dist >= (int)(TILE_WIDTH/(TILE_VERT_WIDTH))
	) {

		out.push_back({code,d_min_sq});
		if(boxes) boxes->add(box);
		return 1;
	}

	TileCode children[4] = {
		tile_code_refine(code,TILE_LOWER_LEFT),
		tile_code_refine(code,TILE_LOWER_RIGHT),
		tile_code_refine(code,TILE_UPPER_LEFT),
		tile_code_refine(code,TILE_UPPER_RIGHT)
	};

	int status = 0;
	for (uint8_t i = 0; i < 4; ++i) {
		status += select_tiles_rec(out, params, children[i]);
	}

	// If status is zero than no tiles were added, so add this tile
	if (!status) {
		out.push_back({code,d_min_sq});
		if (boxes) boxes->add(box);
		return 1;
	}

	return status;
}

// @brief Select tiles within camera frustum based on loaded terrain
// @note The resulting tiles are sorted by distance from the camera.
static void select_tiles(
	select_tiles_params& params,
	std::vector<tile_code_t>& tiles
)
{
	params.res = std::max(params.res, 1e-5);

	std::vector<selection_entry_t> selection;
	for (uint8_t f = 0; f < CUBE_FACES; ++f) {

		TileCode code = {
			.face = f,
			.zoom = 0,
			.idx = 0
		};
		select_tiles_rec(selection, &params, code); 
	}

	constexpr auto comp = [](const selection_entry_t &a, const selection_entry_t &b) {
		return a.dist < b.dist;
	};
	std::sort(selection.begin(), selection.end(), comp);

	size_t count = std::min(selection.size(), params.max_tiles);

	tiles.resize(count);
	for (size_t i = 0; i < count; ++i) {
		tiles[i] = tile_code_pack(selection[i].code);
	}
}

static aabb2_t sub_rect(TileCode parent, TileCode child)
{
	if (parent.zoom >= child.zoom)
		return aabb2_t{.min = glm::dvec2(0), .max = glm::dvec2(1)};

	int diff = child.zoom - parent.zoom;	
	child.idx &= (1 << (2*diff)) - 1;

	return morton_u64_to_rect_f64(child.idx, (uint8_t)diff);
}

static GLuint globe_vao()
{
	GLuint vao;
	glGenVertexArrays(1,&vao);

	glBindVertexArray(vao);

	glEnableVertexArrayAttrib(vao,0);
	glEnableVertexArrayAttrib(vao,1);
	glEnableVertexArrayAttrib(vao,2);

	glVertexAttribFormat(0,3,GL_FLOAT,0,offsetof(GlobeVertex,pos));
	glVertexAttribFormat(1,2,GL_FLOAT,0,offsetof(GlobeVertex,uv));
	glVertexAttribFormat(2,3,GL_FLOAT,0,offsetof(GlobeVertex,normal));

	glVertexAttribBinding(0,0);
	glVertexAttribBinding(1,0);
	glVertexAttribBinding(2,0);

	glBindVertexArray(0);

	return vao;
}


static std::vector<uint32_t> create_tile_indices()
{
	static const uint32_t n = TILE_VERT_WIDTH;
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

static LoadResult create_render_data(ResourceTable *rt, RenderData &data)
{
	LoadResult result = RT_OK;

	std::vector<uint32_t> tile_indices = create_tile_indices();

	data.vao = globe_vao();
	data.material = material_load_file(rt, "material/globe_tile.yaml");

	if (!data.material)
		goto load_failed;

	data.vbo = buffer_create(rt, MAX_TILES*TILE_VERT_COUNT*sizeof(GlobeVertex),
						  GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT);
	if (!data.vbo)
		goto load_failed;

	data.indirect = buffer_create(rt, MAX_TILES*sizeof(DrawCommand),
						  GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT);
	if (!data.indirect)
		goto load_failed;

	data.ssbo = buffer_create(rt, MAX_TILES*sizeof(TileMetadata),
						  GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT);
	if (!data.ssbo)
		goto load_failed;

	data.ibo = buffer_create(rt, sizeof(uint32_t)*6*TILE_VERT_COUNT);
	if (!data.ibo)
		goto load_failed;

	result = buffer_upload(rt, data.ibo, tile_indices.data(), 
			   tile_indices.size()*sizeof(uint32_t));

	if (result)
		goto load_failed;

	return result;
load_failed:
	if (data.vbo) rt->destroy_handle(data.vbo);
	if (data.ibo) rt->destroy_handle(data.ibo);
	if (data.ssbo) rt->destroy_handle(data.ssbo);
	if (data.indirect) rt->destroy_handle(data.indirect);
	return result;
}

static void create_tile_verts(TileCode code, GlobeVertex* out_verts)
{
	uint8_t f = code.face;

	aabb2_t rect = morton_u64_to_rect_f64(code.idx,code.zoom);
	double factor = 1.0/(double)(TILE_VERT_WIDTH-1);

	glm::dvec2 uv;

	size_t idx = 0;
	for (uint32_t i = 0; i < TILE_VERT_WIDTH; ++i) {
		uv.x = (double)i*factor;
		for (uint32_t j = 0; j < TILE_VERT_WIDTH; ++j) {
			uv.y = (double)j*factor;

			glm::dvec2 face_uv = glm::mix(rect.ll(),rect.ur(),uv);
			glm::dvec3 p = cube_to_globe(f, face_uv);

			GlobeVertex vert = {
				.pos = glm::vec3(p),
				.uv = glm::vec2(uv),
				.normal = glm::vec3(p),
			};

			out_verts[idx++] = vert;
		}
	}
}

static void update_draw_cmds(Globe *globe) 
{
	size_t count = globe->selected_tiles.size();

	//-----------------------------------------------------------------------------
	// Indirect Draw Buffer
	const GLBuffer* indirect = globe->rt->get<GLBuffer>(globe->render_data.indirect);

	DrawCommand *cmds = (DrawCommand *)glMapNamedBuffer(indirect->id, GL_WRITE_ONLY);

	for (size_t i = 0; i < count; ++i) {
		uint64_t code = globe->selected_tiles[i];
		size_t slot = globe->tile_allocator->get_idx(code);

		DrawCommand cmd = {
			.count = 6*TILE_VERT_COUNT,
			.instanceCount = 1, 
			.firstIndex = 0,
			.baseVertex = static_cast<int>(slot * TILE_VERT_COUNT),
			.baseInstance = 0
		};

		cmds[i] = cmd;
	}

	glUnmapNamedBuffer(indirect->id);
}

static void update_vbo(Globe *globe)
{
	const TileAllocator::kv_t * new_tiles; 
	size_t new_count;
	globe->tile_allocator->get_new(&new_tiles, &new_count);

	const GLBuffer *buf = globe->rt->get<GLBuffer>(globe->render_data.vbo);
	glBindBuffer(GL_ARRAY_BUFFER, buf->id);

	size_t batch_size = std::max(2*new_count/(std::thread::hardware_concurrency()),1LU);
	std::atomic_int ctr = (int)new_count;

	GlobeVertex *ptr = (GlobeVertex*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

	for (size_t start = 0; start < new_count; start += batch_size) {
		size_t end = std::min(start + batch_size, new_count);
		g_schedule_task([ptr, start, end, new_tiles, &ctr](){
			for (size_t i = start; i < end; ++i) {
				TileAllocator::kv_t kv = new_tiles[i];

				GlobeVertex *dst = ptr + kv.idx * TILE_VERT_COUNT;
				TileCode code = tile_code_unpack(kv.code);
				create_tile_verts(code, dst);
				--ctr;
			}
		});
	}

	while (ctr)
		std::this_thread::yield();

	glUnmapBuffer(GL_ARRAY_BUFFER);
}

static LoadResult update_render_data(
	Globe *globe,
	glm::dvec3 origin,
	const std::vector<uint64_t>& parents, 
	const std::vector<TileGPUIndex> &textures
)
{
	ResourceTable *rt = globe->rt;

	const std::vector<uint64_t>& tiles = globe->selected_tiles;
	uint32_t count = (uint32_t)tiles.size();

	globe->tile_allocator->set(tiles);

	//-----------------------------------------------------------------------------
	// vbo
	
	update_vbo(globe);

	void *ptr;

	//-----------------------------------------------------------------------------
	// tex indices
	
	const GLBuffer *ssbo = rt->get<GLBuffer>(globe->render_data.ssbo);

	glBindBuffer(GL_ARRAY_BUFFER, ssbo->id);
	ptr = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

	TileMetadata *metadata = static_cast<TileMetadata*>(ptr);

	for (uint32_t i = 0; i < count; ++i) {
		uint64_t code_parent = parents[i];
		uint64_t code_child = tiles[i];

		TileGPUIndex idx = textures[i];

		TileCode child = tile_code_unpack(code_child);
		TileCode parent = tile_code_unpack(code_parent);

		aabb2_t rect = morton_u64_to_rect_f64(child.idx,child.zoom);
		aabb2_t rect_tex = parent == TILE_CODE_NONE ? 
			aabb2_t{.min = glm::dvec2(0), .max = glm::dvec2(1)} : 
			sub_rect(parent, child);

		size_t slot = globe->tile_allocator->get_idx(code_child);

		metadata[slot] = {
			.coord = glm::vec4(0),
			.tex_uv = {rect_tex.min, rect_tex.max},
			.globe_uv = {rect.min, rect.max},
			.tex_idx = idx,
			.code_lower = (uint32_t)(code_parent & 0xFFFFFFFF),
			.code_upper = (uint32_t)(code_parent >> 32),
		};
	}

	glUnmapBuffer(GL_ARRAY_BUFFER);

	update_draw_cmds(globe);

	return RT_OK;
};

static double dtime()
{
	static bool init = false; 
	static double s0 = 0;
	if (!init) {
		init = true;
		s0 = dtime();
	}

	auto tpt = std::chrono::high_resolution_clock::now();
	double s = (double)tpt.time_since_epoch().count()/(1e9);
	return s - s0;
}

static void plot_tile_counts(size_t total, size_t new_tiles)
{
		static std::vector<float> times;
		static std::vector<float> new_counts;
		static std::vector<float> tile_counts;
		static size_t scroll = 0;
		static size_t samples = 500;
		static float avg = 0;

		float count = (float)total;

		if (tile_counts.size() < samples) 
			tile_counts.resize(samples,0);
		if (new_counts.size() < samples) 
			new_counts.resize(samples,0);
		if (times.size() < samples) 
			times.resize(samples,0);

		tile_counts[scroll] = count;
		new_counts[scroll] = (float)new_tiles;
		times[scroll] = (float)dtime();

		scroll = (scroll + 1)%samples;

		avg = 0.99f*avg + 0.01f*count;

		if (ImPlot::BeginPlot("Tile selection",ImVec2(200,200))) {
			ImPlot::SetupAxesLimits((double)times[scroll], 
			   (double)times[scroll  ? scroll - 1 : samples - 1], 
			   0, 2*(double)avg,ImPlotCond_Always);
			ImPlot::PlotLine("Tile count", times.data(), 
				tile_counts.data(), (int)samples, ImPlotCond_Always, (int)scroll);
			ImPlot::PlotLine("New count", times.data(), 
				new_counts.data(), (int)samples, ImPlotCond_Always, (int)scroll);
			ImPlot::EndPlot();
		}
}

//------------------------------------------------------------------------------
// Interface

Globe *globe_create(ResourceTable *rt)
{
	std::unique_ptr<Globe> globe(new Globe{});
	globe->rt = rt;

	LoadResult result = create_render_data(rt, globe->render_data);

	if (result != RT_OK)
		return nullptr;

	globe->cpu_cache.reset(
		CPUTileCache::create()
	);
	globe->gpu_cache.reset(
		GPUTileCache::create()
	);
	globe->tile_allocator.reset(
		TileAllocator::create(MAX_TILES)
	);
	globe_init_debug(globe.get());

	return globe.release();
}

void globe_destroy(Globe *globe)
{
	delete globe;
}

void globe_imgui(Globe *globe)
{
	ImGui::Begin("Globe debug");	

	ImGui::SliderInt("res", &globe->dbg.zoom, 0, 8, "%d");
	ImGui::SliderInt("Max zoom", const_cast<int*>(&globe->cpu_cache->m_debug_zoom), 0, 24);

	if (ImGui::Button(globe->dbg.enable_boxes ? 
		"Disable globe tile boxes##globe tile boxes" : 
		"Enable globe tile boxes##globe tile boxes")) 
		globe->dbg.enable_boxes = !globe->dbg.enable_boxes;

	if (ImGui::Button(globe->dbg.fix_camera ? 
			"Unfix globe camera##globe tile camera" : 
			"Fix globe camera##globe tile camera")
	) {
		globe->dbg.fix_camera = !globe->dbg.fix_camera;
	}
	plot_tile_counts(globe->stats.loaded, globe->stats.new_loads);
	ImGui::End();

}

float globe_sample_elevation(const Globe *globe, const glm::dvec3& p)
{
	return globe->cpu_cache->sample_elevation_at(p);
}

LoadResult globe_update(Globe *globe, GlobeUpdateInfo *info)
{
	const Camera * p_camera = info->camera; 

	CPUTileCache *cpu_cache = globe->cpu_cache.get();

	if (globe->dbg.fix_camera)
		p_camera = globe->dbg.camera->get_camera();
	else {
		globe->dbg.camera->set_camera(info->camera);
	}

	globe->selected_tiles.clear();

	//----------------------------------------------------------------------------
	// Adjust the test frustum to only go to the horizon
	
	double resolution = tile_factor((uint8_t)globe->dbg.zoom);

	glm::mat4 pv = p_camera->proj*p_camera->view;
	glm::dvec3 pos = camera_get_pos(p_camera->view);

	frustum_t frust = camera_frustum(pv);

	double r_cull;

	// Adjust culling frustum to only extend enough so that worst-case
	// terrain is visible
	if (true) {
		double h_max = (double)cpu_cache->max();
		double h_min = (double)cpu_cache->min();

		double r_min = 1.0 + h_min;
		double r_max = 1.0 + h_max;

		double r_horizon = sqrt(std::max(dot(pos,pos) - r_min*r_min,0.));
		double r_horizon_max  = sqrt(std::max(r_max * r_max - r_min*r_min,0.));

		glm::dvec3 n_far = frust.p.far.n;
		r_cull = r_horizon + r_horizon_max;
		frust.p.far.d = dot(pos,n_far) + r_cull;
	}

	//-----------------------------------------------------------------------------
	// Process visible tiles
	
	select_tiles_params params = {
		.cpu_cache = cpu_cache,
		.debug = &globe->dbg,
		.max_tiles = MAX_TILES,
		.cull_radius = r_cull,
		.frust = frust,
		.frust_box = frustum_aabb(frust),
		.origin = pos,
		.res = resolution,
	};

	select_tiles(params, globe->selected_tiles);
	size_t count = globe->selected_tiles.size();

	std::vector<uint64_t> loaded_tiles (count, tile_code_pack(TILE_CODE_NONE));
	std::vector<uint64_t> ideal_tiles = globe->selected_tiles;

	for (size_t i = 0; i < ideal_tiles.size(); ++i) {
		TileCode code = tile_code_unpack(ideal_tiles[i]);
		if (code.zoom > 2) {
			code.idx >>= 4;
			code.zoom -= 2;
		}
		ideal_tiles[i] = tile_code_pack(code);
	}
	cpu_cache->load_tiles(count, ideal_tiles.data(), loaded_tiles.data());

	std::vector<TileGPUIndex> tile_textures;

	size_t new_count = globe->gpu_cache->update(
		cpu_cache, loaded_tiles, tile_textures);

	LoadResult result = update_render_data(globe,
		pos, loaded_tiles, tile_textures);
	
	globe->stats.new_loads = new_count;
	globe->stats.loaded = count;

	if (globe->dbg.enable_boxes)
		globe->dbg.boxes->update();

	return result;
}

void globe_draw(const Globe *globe, const RenderContext& ctx)
{
	const RenderData &data = globe->render_data;

	const GLBuffer* vbo = ctx.rt->get<GLBuffer>(data.vbo); 
	const GLBuffer* ibo = ctx.rt->get<GLBuffer>(data.ibo); 
	const GLBuffer* indirect = ctx.rt->get<GLBuffer>(data.indirect); 
	const GLBuffer* ssbo = ctx.rt->get<GLBuffer>(data.ssbo);

	ctx.bind_material(data.material);

	globe->gpu_cache->bind_textures(ctx, 1);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo->id); 
	
	glBindVertexArray(data.vao);

	glBindVertexBuffer(0, vbo->id, 0, sizeof(GlobeVertex));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo->id);
	glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect->id);

	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);

	glMultiDrawElementsIndirect(
		GL_TRIANGLES, 
		GL_UNSIGNED_INT, 
		nullptr, 
		(GLsizei)globe->selected_tiles.size(), 
		sizeof(DrawCommand)	
	);

	glDisable(GL_CULL_FACE);

	glBindVertexArray(0);

	globe_draw_debug(globe, ctx);
}
