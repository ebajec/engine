#include "globe.h"
#include "box_display.h"
#include "gl_debug.h"
#include "camera_controller.h"
#include "geometry.h"

#include <imgui.h>

#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat3x2.hpp>

#include <vector>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cmath>
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
static std::unique_ptr<BoxDisplay> g_disp;

namespace globe
{

static constexpr uint32_t CUBE_FACES = 6;

enum quadrant_t
{
	LOWER_LEFT = 0x0,
	LOWER_RIGHT = 0x1,
	UPPER_LEFT = 0x2,
	UPPER_RIGHT = 0x3
};

void init_boxes(ResourceTable *table) {
	g_disp.reset(new BoxDisplay(table));
	g_enable_boxes = false;
}

void render_boxes(RenderContext const & ctx)
{
	ctx.bind_material(g_disp->material);
	if (g_enable_boxes)
		ctx.draw_cmd_mesh_outline(g_disp->model);
}

void update_boxes() 
{
	if (ImGui::Button(g_enable_boxes ? "Disable globe tile boxes##globe tile boxes" : "Enable globe tile boxes##globe tile boxes")) 
		g_enable_boxes = !g_enable_boxes;

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

static constexpr double tile_res_mult = 1.0;

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

static constexpr double tile_scale_factor = 64;

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

	glm::dvec3 tile_nearest_diff = tile_diff(params->origin,params->origin_uv,rect,code.face);
	double d_min_sq = dot(tile_nearest_diff,params->frust.planes[4].n);
	d_min_sq = std::max(tile_scale_factor * d_min_sq * d_min_sq,1e-5);

	glm::dvec3 mid = cube_to_globe(code.face, 0.5*(rect.ur() + rect.ll()));

	double area = tile_area(code.zoom);

	if ((area/d_min_sq)*tile_res_mult < params->res) {
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
}

static constexpr uint32_t TILE_VERT_WIDTH = 16;
static constexpr uint32_t TILE_VERT_COUNT = 
	TILE_VERT_WIDTH*TILE_VERT_WIDTH;

static void create_mesh(
	double scale,
	glm::dvec3 origin,
	const std::vector<TileCode>& tiles, 
	std::vector<GlobeVertex>& verts,
	std::vector<uint32_t>&indices)
{
	static constexpr uint32_t tile_limit = 4096;

	uint32_t count = std::min((uint32_t)tiles.size(),tile_limit);
	if (tiles.size() > tile_limit) {
		log_warn("Selected tile count is large (%ld)! Limiting to %d", tiles.size(), tile_limit);
	}

	uint32_t total = TILE_VERT_COUNT*count;

	verts.reserve(total);
	indices.reserve(6*total);

	static const uint32_t n = TILE_VERT_WIDTH;

	for (uint32_t i = 0; i < count; ++i) {
		TileCode code = tiles[i];

		uint8_t f = code.face;

		aabb2_t rect = morton_u64_to_rect_f64(code.idx,code.zoom);

		uint32_t offset = static_cast<uint32_t>(verts.size());

		double factor = 1.0/(double)(n-1);

		for (uint32_t i = 0; i < n; ++i) {
			double u = (double)i*factor;
			for (uint32_t j = 0; j < n; ++j) {
				double v = (double)j*factor;

				glm::dvec2 uv = glm::dvec2(u,v);
				glm::dvec2 face_uv = (glm::dvec2(1.0) - uv)*(rect.min) + uv*(rect.max);
				glm::dvec3 p = cube_to_globe(f, face_uv);

				GlobeVertex vert = {
					.pos = glm::vec3(p),
					.uv = glm::vec2(uv),
					.normal = glm::vec3(p),
					.code = code
				};

				verts.push_back(vert);
			}
		}


		for (uint32_t i = 0; i < n; ++i) {
			for (uint32_t j = 0; j < n; ++j) {

				uint32_t in = std::min(i + 1,n - 1);
				uint32_t jn = std::min(j + 1,n - 1);

				indices.push_back(offset + (n) * i  + j); 
				indices.push_back(offset + (n) * in + j); 
				indices.push_back(offset + (n) * in + jn); 
				indices.push_back(offset + (n) * i  + j);
				indices.push_back(offset + (n) * in + jn);
				indices.push_back(offset + (n) * i  + jn);
			}
		}
	}

	return;
};

//------------------------------------------------------------------------------
// Interface

LoadResult globe_create(Globe *globe, ResourceTable *table)
{
	ResourceHandle h = table->create_handle(RESOURCE_TYPE_MODEL);

	if (h == RESOURCE_HANDLE_NULL)
		return RESULT_ERROR;

	LoadResult result = table->allocate(h, nullptr);

	if (result)
		goto load_failed;

	globe->modelID = h;

	return result;
load_failed:
	table->destroy_handle(h);
	return result;
}

LoadResult globe_update(Globe *globe, ResourceTable *table, GlobeUpdateInfo *info)
{
	globe->tiles.clear();
	globe->verts.clear();
	globe->indices.clear();

	glm::mat4 pv = info->camera->proj*info->camera->view;

	frustum_t frust = camera_frustum(pv);

	static int zoom = 3;

	ImGui::Begin("Demo Window");
	ImGui::SliderInt("res", &zoom, 0, 5, "%d");
	ImGui::End();

	double resolution = globe::tile_area((uint8_t)zoom);

	glm::dvec3 pos = camera_get_pos(info->camera->view);

	globe::select_tiles(globe->tiles, frust, pos, resolution);

	globe::create_mesh(1,pos, globe->tiles, globe->verts, globe->indices);

	LoadResult result = table->upload(globe->modelID,"globe",globe);
	
	globe::update_boxes();

	return result;
}

//------------------------------------------------------------------------------
// Loader functions

static LoadResult globe_upload_fn(ResourceTable *table, void *res, void *into);

ResourceLoaderFns loader_fns = { 
	.loader_fn = globe_upload_fn,
	.post_load_fn = nullptr
};

LoadResult globe_upload_fn(ResourceTable *table, void *res, void *usr)
{
	const Globe *globe = static_cast<const Globe*>(usr);
	GLModel *model = static_cast<GLModel*>(res);

	model->type = MODEL_TYPE_MESH_3D;
	model->icount = globe->indices.size();
	model->vcount = globe->verts.size();
	model->vsize = sizeof(GlobeVertex);
	model->isize = sizeof(uint32_t);

	glBindVertexArray(model->vao);


	size_t vsize = globe->verts.size()*sizeof(GlobeVertex);
	glBindBuffer(GL_ARRAY_BUFFER, model->vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizei)vsize, globe->verts.data(), GL_DYNAMIC_DRAW);

	size_t isize = globe->indices.size()*sizeof(uint32_t);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model->ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizei)isize, globe->indices.data(), GL_DYNAMIC_DRAW);

	glEnableVertexArrayAttrib(model->vao,0);
	glEnableVertexArrayAttrib(model->vao,1);
	glEnableVertexArrayAttrib(model->vao,2);
	glEnableVertexArrayAttrib(model->vao,3);
	glEnableVertexArrayAttrib(model->vao,4);
	glVertexAttribPointer(0,3,GL_FLOAT,0,sizeof(GlobeVertex),(void*)offsetof(GlobeVertex,pos));
	glVertexAttribPointer(1,2,GL_FLOAT,0,sizeof(GlobeVertex),(void*)offsetof(GlobeVertex,uv));
	glVertexAttribPointer(2,3,GL_FLOAT,0,sizeof(GlobeVertex),(void*)offsetof(GlobeVertex,normal));
	glVertexAttribIPointer(3,1,GL_UNSIGNED_INT,sizeof(GlobeVertex),(void*)offsetof(GlobeVertex,left));
	glVertexAttribIPointer(4,1,GL_UNSIGNED_INT,sizeof(GlobeVertex),(void*)offsetof(GlobeVertex,right));

	glBindVertexArray(0);
	LoadResult result = RESULT_SUCCESS;

	if (result != RESULT_SUCCESS) {
		log_error("Failed to upload globe mesh");
		return result;
	}

	if (gl_check_err())
		return RESULT_ERROR;

	return result;
}

glm::dmat3 cube_faces_d[] = {
	glm::dmat3( // y ^ z
		0,1,0,
		0,0,1,
		1,0,0
	), glm::dmat3( // x ^ z
		1,0,0,
		0,0,1,
		0,1,0
	), glm::dmat3( // x ^ y
		1,0,0,
		0,1,0,
		0,0,1
	), glm::dmat3( // -y ^ z
		0,-1,0,
		0,0,1,
		-1,0,0
	), glm::dmat3( // - x ^ z
		-1,0,0,
		0,0,1,
		0,-1,0
	), glm::dmat3( // - x ^ x
		-1,0,0,
		0,1,0,
		0,0,-1
	),
};

glm::mat3 cube_faces_f[] = {
	glm::mat3( // y ^ z
		0,1,0,
		0,0,1,
		1,0,0
	), glm::mat3( // x ^ z
		1,0,0,
		0,0,1,
		0,1,0
	), glm::mat3( // x ^ y
		1,0,0,
		0,1,0,
		0,0,1
	), glm::mat3( // -y ^ z
		0,-1,0,
		0,0,1,
		-1,0,0
	), glm::mat3( // - x ^ z
		-1,0,0,
		0,0,1,
		0,-1,0
	), glm::mat3( // - x ^ x
		-1,0,0,
		0,1,0,
		0,0,-1
	),
};

}; // namespace globe
