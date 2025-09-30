#include "renderer/opengl.h"
#include "renderer/gl_debug.h"

#include "resource/buffer.h"

#include "utils/camera.h"
#include "utils/geometry.h"
#include "utils/thread_pool.h"
#include "utils/geometry.h"

#include "globe/globe.h"
#include "globe/cpu_cache.h"
#include "globe/gpu_cache.h"

#include <debug/box_debug_view.h>
#include <debug/camera_debug_view.h>

#include <imgui.h>
#include <implot.h>

#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat3x2.hpp>

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

#ifndef KILOBYTE
#define KILOBYTE ((size_t)1024)
#endif

#ifndef MEGABYTE
#define MEGABYTE (KILOBYTE*KILOBYTE)
#endif

#ifndef GIGABYTE
#define GIGABYTE (MEGABYTE*KILOBYTE)
#endif

static bool g_enable_boxes;
static std::unique_ptr<BoxDebugView> g_disp;
static std::unique_ptr<CameraDebugView> g_camera_debug;

namespace globe
{
static constexpr uint32_t TILE_VERT_WIDTH = 32;
static constexpr uint32_t TILE_VERT_COUNT = 
	TILE_VERT_WIDTH*TILE_VERT_WIDTH;

static constexpr double tile_scale_factor = 32;

struct select_tiles_params
{
	const TileDataSource * source;

	frustum_t frust;
	aabb3_t frust_box;
	glm::dvec3 origin;
	double res;
};

enum quadrant_t
{
	LOWER_LEFT = 0x0,
	LOWER_RIGHT = 0x1,
	UPPER_LEFT = 0x2,
	UPPER_RIGHT = 0x3
};

static void init_debug(ResourceTable *table) {
	g_disp.reset(new BoxDebugView(table));
	g_camera_debug.reset(new CameraDebugView(table));
	g_enable_boxes = false;
}

static void draw_boxes(RenderContext const & ctx)
{
	ctx.bind_material(g_disp->material);
	if (g_enable_boxes)
		ctx.draw_cmd_mesh_outline(g_disp->model);

	g_camera_debug->render(ctx);
}

static void update_boxes() 
{
	if (g_enable_boxes)
		g_disp->update();
}

static constexpr TileCode tile_code_refine(TileCode code, quadrant_t quadrant)
{
	++code.zoom;
	code.idx <<= 2;
	code.idx |= quadrant;
	return code;
}

/*
static glm::dvec3 tile_diff(glm::dvec3 p, glm::dvec2 p_uv, aabb2_t tile, uint8_t face)
{
	p_uv = glm::clamp(p_uv,tile.min,tile.max);

	glm::dvec3 diff = p - cube_to_globe(face, p_uv);

	return diff;
}
*/

struct select_tiles_rec_params : select_tiles_params
{
	glm::dvec2 origin_uv;
};

static aabb3_t tile_box(const TileDataSource *source, TileCode code) 
{
	aabb2_t rect = morton_u64_to_rect_f64(code.idx,code.zoom);
	glm::dvec2 mid_uv = 0.5*(rect.ur() + rect.ll());

	// TODO: Don't sample here if it can be avoided (i.e., compute min/max in 
	// a quadtree for tiles)
	
	float samples[5] = {
         source->sample_elevation_at(rect.ll(),code.face),
         source->sample_elevation_at(rect.lr(),code.face),
         source->sample_elevation_at(rect.ul(),code.face),
         source->sample_elevation_at(rect.ur(),code.face),
         source->sample_elevation_at(mid_uv,code.face)   
	};

	glm::dvec3 p[5] = {
		(1.0 + (double)samples[0])*cube_to_globe(code.face, rect.ll()),
		(1.0 + (double)samples[1])*cube_to_globe(code.face, rect.lr()),
		(1.0 + (double)samples[2])*cube_to_globe(code.face, rect.ul()),
		(1.0 + (double)samples[3])*cube_to_globe(code.face, rect.ur()),
		(1.0 + (double)samples[4])*cube_to_globe(code.face, mid_uv),
	};

	aabb3_t box = aabb3_bounding(p, sizeof(p)/sizeof(p[0]));

	if (code.zoom == 0) {
		box = aabb3_add(box, cube_to_globe(code.face, 0.5*(rect.ll() + rect.lr())));
		box = aabb3_add(box, cube_to_globe(code.face, 0.5*(rect.ll() + rect.ul())));
		box = aabb3_add(box, cube_to_globe(code.face, 0.5*(rect.ur() + rect.ul())));
		box = aabb3_add(box, cube_to_globe(code.face, 0.5*(rect.ur() + rect.lr())));
	}

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
	const bool enable_boxes = g_enable_boxes;

	if (code.zoom > 23)
		return 0;

	aabb3_t box = tile_box(params->source,code);

	if (!intersects(box, params->frust_box))
		return 0;

	for (uint8_t i = 0; i < 6; ++i) {
		if (classify(box, params->frust.planes[i]) > 0) { 
			return 0;
		}
	}

	double d_min_sq = aabb3_dist_sq(box, params->origin);
	d_min_sq = std::max(tile_scale_factor * d_min_sq,1e-6);

	double area = tile_factor(code.zoom);

	if (area/d_min_sq < params->res) {
		out.push_back({code,d_min_sq});
		if(enable_boxes) g_disp->add(box);
		return 1;
	}

	TileCode children[4] = {
		tile_code_refine(code,LOWER_LEFT),
		tile_code_refine(code,LOWER_RIGHT),
		tile_code_refine(code,UPPER_LEFT),
		tile_code_refine(code,UPPER_RIGHT)
	};

	int status = 0;
	for (uint8_t i = 0; i < 4; ++i) {
		status += select_tiles_rec(out, params, children[i]);
	}

	// If status is zero than no tiles were added, so add this tile
	if (!status) {
		out.push_back({code,d_min_sq});
		if (enable_boxes) g_disp->add(box);
		return 1;
	}

	return status;
}

static void select_tiles(
	select_tiles_params& params,
	std::vector<TileCode>& tiles
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
	tiles.resize(selection.size());

	for (size_t i = 0; i < selection.size(); ++i) {
		tiles[i] = selection[i].code;
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

/*
 * Partial derivatives
static glm::vec3 globe_tangent(glm::dvec2 uv)
{
	double u = uv.x;
	double v = uv.y;
	double frac = pow(1 + u*u + v*v,-3.0/2.0);
	return glm::dvec3(1 + v*v, -u*v, -u)*frac;
}
*/

static void create_tile_verts(TileCode code, TileCode parent, GlobeVertex* out_verts)
{
	uint8_t f = code.face;

	aabb2_t rect = morton_u64_to_rect_f64(code.idx,code.zoom);
	aabb2_t rect_tex = parent == TILE_CODE_NONE ? 
		aabb2_t{.min = glm::dvec2(0), .max = glm::dvec2(1)} : 
		sub_rect(parent, code);

	double factor = 1.0/(double)(TILE_VERT_WIDTH-1);

	glm::dvec2 uv;

	uint64_t packed_parent = tile_code_pack(parent);

	size_t idx = 0;
	for (uint32_t i = 0; i < TILE_VERT_WIDTH; ++i) {
		uv.x = (double)i*factor;
		for (uint32_t j = 0; j < TILE_VERT_WIDTH; ++j) {
			uv.y = (double)j*factor;

			glm::dvec2 face_uv = glm::mix(rect.ll(),rect.ur(),uv);
			glm::dvec2 tex_uv = glm::mix(rect_tex.ll(),rect_tex.ur(),uv);
			glm::dvec3 p = cube_to_globe(f, face_uv);

			GlobeVertex vert = {
				.pos = glm::vec3(p),
				.uv = glm::vec2(tex_uv),
				.normal = glm::vec3(p),
				.big_uv = face_uv,
				.code = {
					.left = (uint32_t)(packed_parent & 0xFFFFFFFF),
					.right = (uint32_t)((packed_parent >> 32)), 
				}
			};

			out_verts[idx++] = vert;
		}
	}
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

static void draw_cmds(DrawCommand * cmds, size_t count) 
{
	for (size_t i = 0; i < count; ++i) {
		DrawCommand cmd = {
			.count = 6*TILE_VERT_COUNT,
			.instanceCount = 1, 
			.firstIndex = 0,
			.baseVertex = static_cast<int>(i * TILE_VERT_COUNT),
			.baseInstance = 0
		};

		cmds[i] = cmd;
	}
}

static GLuint globe_vao()
{
	GLuint vao;
	glGenVertexArrays(1,&vao);

	glBindVertexArray(vao);

	glEnableVertexArrayAttrib(vao,0);
	glEnableVertexArrayAttrib(vao,1);
	glEnableVertexArrayAttrib(vao,2);
	glEnableVertexArrayAttrib(vao,3);
	glEnableVertexArrayAttrib(vao,4);
	glEnableVertexArrayAttrib(vao,5);

	glVertexAttribFormat(0,3,GL_FLOAT,0,offsetof(GlobeVertex,pos));
	glVertexAttribFormat(1,2,GL_FLOAT,0,offsetof(GlobeVertex,uv));
	glVertexAttribFormat(2,3,GL_FLOAT,0,offsetof(GlobeVertex,normal));
	glVertexAttribFormat(3,2,GL_FLOAT,0,offsetof(GlobeVertex,big_uv));
	glVertexAttribIFormat(4,1,GL_UNSIGNED_INT,offsetof(GlobeVertex,code.left));
	glVertexAttribIFormat(5,1,GL_UNSIGNED_INT,offsetof(GlobeVertex,code.right));

	glVertexAttribBinding(0,0);
	glVertexAttribBinding(1,0);
	glVertexAttribBinding(2,0);
	glVertexAttribBinding(3,0);
	glVertexAttribBinding(4,0);
	glVertexAttribBinding(5,0);

	glBindVertexArray(0);

	return vao;
}

struct alignas(16) TileMetadata
{
	glm::vec4 coord;
	TileGPUIndex tex_idx;
	
};

static LoadResult create_render_data(ResourceTable *rt, RenderData &data)
{
	LoadResult result = RESULT_SUCCESS;

	std::vector<uint32_t> tile_indices = create_tile_indices();

	data.vao = globe_vao();
	data.material = material_load_file(rt, "material/globe_tile.yaml");

	if (!data.material)
		goto load_failed;

	data.vbo = create_buffer(rt, MAX_TILES*TILE_VERT_COUNT*sizeof(GlobeVertex),
						  GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT);
	if (!data.vbo)
		goto load_failed;

	data.indirect = create_buffer(rt, MAX_TILES*sizeof(DrawCommand),
						  GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT);
	if (!data.vbo)
		goto load_failed;

	data.ssbo = create_buffer(rt, MAX_TILES*sizeof(TileMetadata),
						  GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT);
	if (!data.ssbo)
		goto load_failed;

	data.ibo = create_buffer(rt, sizeof(uint32_t)*6*TILE_VERT_COUNT);
	if (!data.ibo)
		goto load_failed;

	result = upload_buffer(rt, data.ibo, tile_indices.data(), 
			   tile_indices.size()*sizeof(uint32_t));

	if (result)
		goto load_failed;

	return result;
load_failed:
	if (data.vbo) rt->destroy_handle(data.vbo);
	if (data.ibo) rt->destroy_handle(data.ibo);
	return RESULT_ERROR;
}

static LoadResult update_render_data(
	ResourceTable *rt,
	Globe *globe,
	glm::dvec3 origin,
	const std::vector<TileCode>& parents, 
	const std::vector<TileGPUIndex> &textures
)
{
	const std::vector<TileCode>& tiles = globe->tiles;

	uint32_t count = std::min((uint32_t)tiles.size(),(uint32_t)MAX_TILES);
	if (tiles.size() > MAX_TILES) {
		log_warn("Selected tile count is large (%ld)! Limiting to %d", 
			tiles.size(), MAX_TILES);
	}

	//uint32_t cell_count = 0;
	//std::unordered_map<TileCode, uint32_t, TileCodeHash> map;

	//for (uint32_t i = 0; i < count; ++i) {
	//	TileCode code = tiles[i];
	//	TileCode cell = tile_cell_index(code);
	//	auto it = map.find(cell);
	//	if (it == map.end()) {
	//		map[cell] = ++cell_count;
	//	}
	//}

	//double dmin = DBL_MAX;
	//uint32_t argmin = 0;

	//std::vector<glm::dvec4> coords;
	//coords.reserve(cell_count);

	//for (auto [cell, idx] : map) {
	//	//glm::dmat3 frame = tile_frame(cell);
	//	glm::dvec4 T = cell_transform(cell);
	//	coords.push_back(T);

	//	double d = glm::length(origin - glm::dvec3(T)); 

	//	if (d < dmin) {
	//		dmin = d;
	//		argmin = static_cast<uint32_t>(coords.size() - 1);
	//	}
	//}

	//glm::dvec4 origin_coords = glm::dvec4(origin,coords[argmin].w);

	//for (glm::dvec4& coord : coords) {
	//	glm::dvec3 r = glm::dvec3(coord) - origin;
	//	double s = origin_coords.w/coord.w;
	//	coord = glm::dvec4(r,s);
	//}

	//uint32_t total = TILE_VERT_COUNT*count;

	//-----------------------------------------------------------------------------
	// vbo
	
	const GLBuffer *buf = rt->get<GLBuffer>(globe->render_data.vbo);

	glBindBuffer(GL_ARRAY_BUFFER, buf->id);
	void *ptr = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

	uint32_t offset = 0;

	std::atomic_uint32_t ctr = count;

	for (uint32_t i = 0; i < count; ++i) {
		offset = i * TILE_VERT_COUNT;	

		TileCode code = tiles[i];
		GlobeVertex * dst = (GlobeVertex*)ptr + offset;
		TileCode parent = parents[i];

		g_schedule_task([=,&ctr](){
			create_tile_verts(code, parent, dst);
			--ctr;	
		});
	}

	while (ctr > 0)
		std::this_thread::yield();

	glUnmapBuffer(GL_ARRAY_BUFFER);

	//-----------------------------------------------------------------------------
	// tex indices
	
	const GLBuffer *ssbo = rt->get<GLBuffer>(globe->render_data.ssbo);

	glBindBuffer(GL_ARRAY_BUFFER, ssbo->id);
	ptr = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

	TileMetadata *metadata = static_cast<TileMetadata*>(ptr);

	for (uint32_t i = 0; i < count; ++i) {
		// TODO : Triple indirection...
		metadata[i] = {
			.coord = glm::vec4(0),
			.tex_idx = textures[i],
		};
	}

	glUnmapBuffer(GL_ARRAY_BUFFER);

	//-----------------------------------------------------------------------------
	// Indirect Draw Buffer
	 const GLBuffer* indirect = rt->get<GLBuffer>(globe->render_data.indirect);

	ptr = glMapNamedBuffer(indirect->id, GL_WRITE_ONLY);
	draw_cmds(static_cast<DrawCommand*>(ptr), count);
	glUnmapNamedBuffer(indirect->id);

	return RESULT_SUCCESS;
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

LoadResult globe_create(Globe *globe, ResourceTable *rt)
{
	LoadResult result = create_render_data(rt, globe->render_data);

	if (result != RESULT_SUCCESS)
		goto load_failed;

	globe::init_debug(rt);

	globe->cpu_cache.reset(
		TileCPUCache::create(TILE_SIZE*sizeof(float),(size_t)1*GIGABYTE)
	);
	globe->source.reset(
		TileDataSource::create()
	);
	globe->gpu_cache.reset(
		new TileGPUCache{}
	);

	return result;
load_failed:
	return result;
}

LoadResult globe_update(Globe *globe, ResourceTable *rt, GlobeUpdateInfo *info)
{
	const Camera * p_camera = g_camera_debug->get_camera(); 
	static int zoom = 5;

	ImGui::Begin("Globe debug");	

	ImGui::SliderInt("res", &zoom, 0, 8, "%d");
	ImGui::SliderInt("Max zoom", const_cast<int*>(&globe->source->m_debug_zoom), 0, 24);

	if (ImGui::Button(g_enable_boxes ? 
		"Disable globe tile boxes##globe tile boxes" : 
		"Enable globe tile boxes##globe tile boxes")) 
		g_enable_boxes = !g_enable_boxes;

	if (ImGui::Button(p_camera ? 
			"Unfix globe camera##globe tile camera" : 
			"Fix globe camera##globe tile camera")
	) {
		g_camera_debug->set_camera(p_camera ? nullptr : info->camera);
		p_camera = info->camera;
	}
	if (!p_camera) p_camera = info->camera;

	globe->tiles.clear();

	//----------------------------------------------------------------------------
	// Adjust the test frustum to only go to the horizon
	
	glm::mat4 pv = p_camera->proj*p_camera->view;
	frustum_t frust = camera_frustum(pv);
	double resolution = tile_factor((uint8_t)zoom);
	glm::dvec3 pos = camera_get_pos(p_camera->view);

	if (true) {
		static double h_max = DATA_SOURCE_TEST_AMP;
		static double h_min = -h_max;

		double r_min = 1.0 + h_min;
		double r_max = 1.0 + h_max;

		double r_horizon = sqrt(std::max(dot(pos,pos) - r_min*r_min,0.));
		double r_horizon_max  = sqrt(std::max(r_max * r_max - r_min*r_min,0.));

		glm::dvec3 n_far = frust.p.far.n;
		frust.p.far.d = dot(pos,n_far) + r_horizon + r_horizon_max;
	}

	//-----------------------------------------------------------------------------
	// Process visible tiles
	
	select_tiles_params params = {
		.source = globe->source.get(),
		.frust = frust,
		.frust_box = frustum_aabb(frust),
		.origin = pos,
		.res = resolution,
	};

	g_disp->add(params.frust_box);

	globe::select_tiles(params, globe->tiles);

	size_t count = std::min(globe->tiles.size(),(size_t)MAX_TILES);

	globe->tiles.resize(count);
	std::vector<TileCode> data_tiles = globe->cpu_cache->update(
		*globe->source, globe->tiles);

	std::vector<TileGPUIndex> tile_textures;

	size_t new_count = globe->gpu_cache->update(globe->cpu_cache.get(),
											 data_tiles,tile_textures);

	LoadResult result = update_render_data(rt,globe,
		pos,data_tiles,tile_textures);
	
	globe::update_boxes();
	plot_tile_counts(count, new_count);
	ImGui::End();

	globe->loaded_count = count;

	return result;
}

void globe_draw(const RenderContext& ctx, const Globe *globe)
{
	const RenderData &data = globe->render_data;

	const GLBuffer* vbo = ctx.rt->get<GLBuffer>(data.vbo); 
	const GLBuffer* ibo = ctx.rt->get<GLBuffer>(data.ibo); 
	const GLBuffer* indirect = ctx.rt->get<GLBuffer>(data.indirect); 
	const GLBuffer* ssbo = ctx.rt->get<GLBuffer>(data.ssbo);

	ctx.bind_material(data.material);

	globe->gpu_cache->bind_texture_arrays(1);

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
		(GLsizei)globe->loaded_count, 
		sizeof(DrawCommand)	
	);

	glDisable(GL_CULL_FACE);

	glBindVertexArray(0);

	globe::draw_boxes(ctx);
}

}; // namespace globe
