#include "globe.h"
#include "gl_debug.h"
#include "camera_controller.h"
#include "geometry.h"
#include "buffer.h"

#include "tile_cache.h"

#include "debug/box_debug_view.h"
#include "debug/camera_debug_view.h"

#include <imgui.h>

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

static bool g_enable_boxes;
static std::unique_ptr<BoxDebugView> g_disp;
static std::unique_ptr<CameraDebugView> g_camera_debug;

namespace globe
{

static constexpr uint32_t CUBE_FACES = 6;

static constexpr uint32_t TILE_VERT_WIDTH = 32;
static constexpr uint32_t TILE_VERT_COUNT = 
	TILE_VERT_WIDTH*TILE_VERT_WIDTH;

static constexpr double tile_scale_factor = 128;

enum quadrant_t
{
	LOWER_LEFT = 0x0,
	LOWER_RIGHT = 0x1,
	UPPER_LEFT = 0x2,
	UPPER_RIGHT = 0x3
};

void init_debug(ResourceTable *table) {
	g_disp.reset(new BoxDebugView(table));
	g_camera_debug.reset(new CameraDebugView(table));
	g_enable_boxes = false;
}

void draw_boxes(RenderContext const & ctx)
{
	ctx.bind_material(g_disp->material);
	if (g_enable_boxes)
		ctx.draw_cmd_mesh_outline(g_disp->model);

	g_camera_debug->render(ctx);
}

void update_boxes() 
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

struct select_tiles_params
{
	frustum_t frust;
	double res;
	glm::dvec3 origin;
	glm::dvec2 origin_uv;
};

static glm::dvec3 tile_diff(glm::dvec3 p, glm::dvec2 p_uv, aabb2_t tile, uint8_t face)
{
	p_uv = glm::clamp(p_uv,tile.min,tile.max);

	glm::dvec3 diff = p - cube_to_globe(face, p_uv);

	return diff;
}

static inline int select_tiles_rec(
	std::vector<TileCode> &out, 
	const select_tiles_params *params,
	TileCode code)
{
	const bool enable_boxes = g_enable_boxes;

	if (code.zoom > 23)
		return 0;

	aabb2_t rect = morton_u64_to_rect_f64(code.idx,code.zoom);

	glm::dvec3 p[5] = {
		cube_to_globe(code.face, rect.ll()),
		cube_to_globe(code.face, rect.lr()),
		cube_to_globe(code.face, rect.ul()),
		cube_to_globe(code.face, rect.ur()),

		// the last one is not always necessary right now, but will be
		// with elevation
		cube_to_globe(code.face, 0.5*(rect.ur() + rect.ll())),
	};

	aabb3_t box = aabb_bounding(p, sizeof(p)/sizeof(p[0]));

	if (code.zoom == 0) {
		box = aabb_add(box, cube_to_globe(code.face, 0.5*(rect.ll() + rect.lr())));
		box = aabb_add(box, cube_to_globe(code.face, 0.5*(rect.ll() + rect.ul())));
		box = aabb_add(box, cube_to_globe(code.face, 0.5*(rect.ur() + rect.ul())));
		box = aabb_add(box, cube_to_globe(code.face, 0.5*(rect.ur() + rect.lr())));
	}

	for (uint8_t i = 0; i < 6; ++i) {
		if (classify(box, params->frust.planes[i]) > 0) { 
			return 0;
		}
	}

	// TODO : Use the integral of the product of view direction and surface normal
	// over a tile to approximate the visible area.
	
	glm::dvec3 tile_nearest_diff = tile_diff(params->origin,params->origin_uv,rect,code.face);
	double d_min_sq = dot(tile_nearest_diff,params->frust.planes[4].n);
	d_min_sq = std::max(tile_scale_factor * d_min_sq * d_min_sq,1e-6);

	glm::dvec3 mid = cube_to_globe(code.face, 0.5*(rect.ur() + rect.ll()));

	double area = tile_area(code.zoom);

	if (area/d_min_sq < params->res) {
		out.push_back(code);
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
		out.push_back(code);
		if (enable_boxes) g_disp->add(box);
		return 1;
	}

	return status;
}


void select_tiles(
	std::vector<TileCode>& tiles,
	const frustum_t& frust, 
	glm::dvec3 center, 
	double res
)
{
	res = std::max(res,1e-5);

	select_tiles_params params = {
		.frust = frust,
		.res = res,
		.origin = center,
		.origin_uv = glm::vec2(0)
	};
 
	for (uint8_t f = 0; f < CUBE_FACES; ++f) {
		params.origin_uv = globe_to_cube_face(center,f);

		TileCode code = {
			.face = f,
			.zoom = 0,
			.idx = 0
		};
		select_tiles_rec(tiles, &params, code); 
	}

	log_info("globe::select_tiles : selected %d tiles",tiles.size());
}

aabb2_t sub_rect(TileCode parent, TileCode child)
{
	if (parent.zoom >= child.zoom)
		return aabb2_t{.x0 = 0,.y0 = 0, .x1 = 1, .y1 = 1};

	int diff = child.zoom - parent.zoom;	
	child.idx &= (1 << (2*diff)) - 1;

	return morton_u64_to_rect_f64(child.idx, (uint8_t)diff);
}

GLuint globe_vao()
{
	GLuint vao;
	glGenVertexArrays(1,&vao);

	glBindVertexArray(vao);

	glEnableVertexArrayAttrib(vao,0);
	glEnableVertexArrayAttrib(vao,1);
	glEnableVertexArrayAttrib(vao,2);
	glEnableVertexArrayAttrib(vao,3);
	glEnableVertexArrayAttrib(vao,4);

	glVertexAttribFormat(0,3,GL_FLOAT,0,offsetof(GlobeVertex,pos));
	glVertexAttribFormat(1,2,GL_FLOAT,0,offsetof(GlobeVertex,uv));
	glVertexAttribFormat(2,3,GL_FLOAT,0,offsetof(GlobeVertex,normal));
	glVertexAttribIFormat(3,1,GL_UNSIGNED_INT,offsetof(GlobeVertex,left));
	glVertexAttribIFormat(4,1,GL_UNSIGNED_INT,offsetof(GlobeVertex,right));

	glVertexAttribBinding(0,0);
	glVertexAttribBinding(1,0);
	glVertexAttribBinding(2,0);
	glVertexAttribBinding(3,0);
	glVertexAttribBinding(4,0);

	glBindVertexArray(0);

	return vao;
}


static void create_tile_verts(TileCode code, TileCode parent, GlobeVertex* out_verts)
{
	uint8_t f = code.face;

	aabb2_t rect = morton_u64_to_rect_f64(code.idx,code.zoom);
	aabb2_t rect_tex = sub_rect(parent, code);

	double factor = 1.0/(double)(TILE_VERT_WIDTH-1);

	glm::dvec2 uv;

	size_t idx = 0;
	for (uint32_t i = 0; i < TILE_VERT_WIDTH; ++i) {
		uv.x = (double)i*factor;
		for (uint32_t j = 0; j < TILE_VERT_WIDTH; ++j) {
			uv.y = (double)j*factor;

			glm::dvec2 face_uv = glm::mix(rect.min,rect.max,uv);
			glm::dvec2 tex_uv = glm::mix(rect_tex.min,rect_tex.max,uv);
			glm::dvec3 p = cube_to_globe(f, face_uv);

			GlobeVertex vert = {
				.pos = glm::vec3(p),
				.uv = glm::vec2(tex_uv),
				.normal = glm::vec3(p),
				.code = code
			};

			out_verts[idx++] = vert;
		}
	}
}

std::vector<DrawCommand> draw_cmds(size_t count) 
{
	std::vector<DrawCommand> cmds (count);

	size_t index_count = 6 * TILE_VERT_COUNT;

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

	return cmds;
}

std::vector<uint32_t> create_tile_indices()
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

LoadResult create_render_data(ResourceTable *rt, RenderData &data)
{
	LoadResult result = RESULT_SUCCESS;

	std::vector<uint32_t> tile_indices = create_tile_indices();

	data.vao = globe_vao();
	data.material = load_material_file(rt, "material/globe_tile.yaml");

	if (!data.material)
		goto load_failed;

	data.vbo = create_buffer(rt, MAX_TILES*TILE_VERT_COUNT*sizeof(GlobeVertex),
						  GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT);

	if (!data.vbo)
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
	const std::vector<TileTexIndex> &textures
)
{
	const std::vector<TileCode>& tiles = globe->tiles;
	std::vector<GlobeVertex>& verts = globe->verts;

	uint32_t count = std::min((uint32_t)tiles.size(),(uint32_t)MAX_TILES);
	if (tiles.size() > MAX_TILES) {
		log_warn("Selected tile count is large (%ld)! Limiting to %d", tiles.size(), MAX_TILES);
	}

	uint32_t total = TILE_VERT_COUNT*count;

	log_info("globe::create_mesh : total globe vertices : %d",total);

	verts.resize(total);
	uint32_t offset = 0;
	for (uint32_t i = 0; i < count; ++i) {
		size_t offset = i * TILE_VERT_COUNT;	

		TileCode code = tiles[i];
		create_tile_verts(code, parents[i], &verts[offset]);
	}

	size_t verts_bytes = verts.size()*sizeof(GlobeVertex);

	const GLBuffer *buf = rt->get<GLBuffer>(globe->render_data.vbo);

	glBindBuffer(GL_ARRAY_BUFFER, buf->id);
	void *ptr = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

	memcpy(ptr, verts.data(), verts_bytes);

	glUnmapBuffer(GL_ARRAY_BUFFER);

	globe->render_data.cmds = draw_cmds(count);

	return RESULT_SUCCESS;
};


//------------------------------------------------------------------------------
// Interface

LoadResult globe_create(Globe *globe, ResourceTable *rt)
{
	LoadResult result = create_render_data(rt, globe->render_data);

	if (result != RESULT_SUCCESS)
		goto load_failed;

	globe::init_debug(rt);

	return result;
load_failed:
	return result;
}

LoadResult globe_update(Globe *globe, ResourceTable *rt, GlobeUpdateInfo *info)
{
	globe->tiles.clear();
	globe->verts.clear();

	const Camera * p_camera = g_camera_debug->get_camera(); 
	static int zoom = 3;

	ImGui::Begin("Globe debug");
	ImGui::SliderInt("res", &zoom, 0, 8, "%d");

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

	ImGui::End();

	//----------------------------------------------------------------------------
	// Adjust the test frustum to only go to the horizon
	
	glm::mat4 pv = p_camera->proj*p_camera->view;
	frustum_t frust = camera_frustum(pv);

	double resolution = globe::tile_area((uint8_t)zoom);
	glm::dvec3 pos = camera_get_pos(p_camera->view);
	double r_horizon = sqrt(std::max(dot(pos,pos) - 1.,0.));

	glm::dvec3 n_far = frust.far.n;
	frust.far.d = dot(pos,n_far) + r_horizon;

	//-----------------------------------------------------------------------------
	// Process visible tiles

	globe::select_tiles(globe->tiles, frust, pos, resolution);

	std::vector<TileTexIndex> tile_textures;
	std::vector<std::pair<TileCode,TileTexIndex>> new_textures;
	globe->cache.get_textures(globe->tiles,tile_textures,new_textures);

	LoadResult result = update_render_data(rt,globe,pos,globe->tiles,tile_textures);
	
	globe::update_boxes();

	return result;
}

void globe_record_draw_cmds(const RenderContext& ctx, const Globe *globe)
{
	const RenderData &data = globe->render_data;

	ctx.bind_material(data.material);

	const GLBuffer* vbo = ctx.table->get<GLBuffer>(data.vbo); 
	const GLBuffer* ibo = ctx.table->get<GLBuffer>(data.ibo); 

	glBindVertexArray(data.vao);

	glBindVertexBuffer(0, vbo->id, 0, sizeof(GlobeVertex));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo->id);

	glMultiDrawElementsIndirect(
		GL_TRIANGLES, 
		GL_UNSIGNED_INT, 
		data.cmds.data(), 
		data.cmds.size(), 
		0
	);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);

	glBindVertexArray(0);

	globe::draw_boxes(ctx);
}

}; // namespace globe
