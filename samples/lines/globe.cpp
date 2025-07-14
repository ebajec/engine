#include "globe.h"
#include "box_display.h"

#include <glm/vec3.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat3x2.hpp>

#include <cstdint>
#include <climits>
#include <cfloat>
#include <cmath>
#include <vector>
#include <complex.h>

#ifndef PI 
#define PI 3.14159265359
#endif

#ifndef QUATPI 
#define QUATPI 0.785398163397
#endif

static std::unique_ptr<BoxDisplay> g_disp;
static void init_boxes(ResourceLoader *loader) {
	g_disp.reset(new BoxDisplay(loader));
}

namespace globe
{

static constexpr uint32_t CUBE_FACES = 6;

constexpr uint8_t TILE_MAX_ZOOM = 29;

constexpr uint64_t TILE_CODE_FACE_BITS_MASK = 0x8500000000000000;
constexpr uint8_t TILE_CODE_FACE_BITS_SHIFT = 61;

constexpr uint64_t TILE_CODE_ZOOM_BITS_MASK = 0x1700000000000000;
constexpr uint64_t TILE_CODE_ZOOM_BITS_SHIFT = 56;

constexpr uint64_t TILE_CODE_IDX_BITS_MASK = 
	~(TILE_CODE_FACE_BITS_MASK | TILE_CODE_ZOOM_BITS_MASK);

enum quadrant_t
{
	LOWER_LEFT = 0x0,
	LOWER_RIGHT = 0x1,
	UPPER_LEFT = 0x2,
	UPPER_RIGHT = 0x3
};

static constexpr tile_code_t tile_code_refine(tile_code_t code, quadrant_t quadrant)
{
	++code.zoom;
	code.idx <<= 2;
	code.idx |= quadrant;
	return code;
}

static inline int select_tiles_rec(
	std::vector<tile_code_t> &out, 
	const frustum_t &frust,
	glm::dvec3 origin,
	double res,
	tile_code_t code)
{
	if (code.zoom > 23)
		return 0;

	aabb2_t rect = morton_u64_to_rect_f64(code.idx,code.zoom);

	glm::dvec3 p[4] = {
		cube_to_globe(code.face, rect.ll()),
		cube_to_globe(code.face, rect.lr()),
		cube_to_globe(code.face, rect.ul()),
		cube_to_globe(code.face, rect.ur()),
	};

	size_t count = sizeof(p)/sizeof(glm::dvec3);

	// TODO: Improve accuracy of this.
	double prod_max = -DBL_MAX;
	for (uint8_t i = 0; i < count; ++i) {
		prod_max = std::max(prod_max,dot(p[i] - origin,frust.planes[0].n));
	}

	// Discard if not facing camera
	if (prod_max < 0)
		return 0;


	aabb3_t box = bounding(p, count);

	/*
	uint8_t hits = 0;
	for (uint8_t i = 0; i < 6; ++i) {
		if (classify(box, frust.planes[i]) < 0) { 
			return 0;
		}
	}
	*/

	double d_min_sq = 64*aabb3_dist_sq(box,origin);
	double area = tile_area(code.zoom);

	if ((area/d_min_sq) < res) {
		out.push_back(code);
		return 1;
	}

	tile_code_t children[4] = {
		tile_code_refine(code,LOWER_LEFT),
		tile_code_refine(code,LOWER_RIGHT),
		tile_code_refine(code,UPPER_LEFT),
		tile_code_refine(code,UPPER_RIGHT)
	};

	int status = 0;
	for (uint8_t i = 0; i < 4; ++i) {
		status += select_tiles_rec(out, frust, origin, res, children[i]);
	}

	// If status is zero than no tiles were added, so add this tile
	if (!status) {
		out.push_back(code);
		return 1;
	}

	return status;
}


void select_tiles(
	std::vector<tile_code_t>& tiles,
	const frustum_t& frust, 
	glm::dvec3 center, 
	double res
)
{
	res = std::max(res,1e-5);

	static double max_radius = 1;
 
	for (uint8_t f = 0; f < CUBE_FACES; ++f) {
		tile_code_t code = {
			.face = f,
			.zoom = 0,
			.idx = 0
		};
		select_tiles_rec(tiles, frust, center, res, code); 
	}
}

static constexpr uint32_t TILE_VERT_WIDTH = 16;
static constexpr uint32_t TILE_VERT_COUNT = 
	TILE_VERT_WIDTH*TILE_VERT_WIDTH;

void create_mesh(
	double scale,
	glm::dvec3 origin,
	const std::vector<tile_code_t>& tiles, 
	std::vector<vertex3d>& verts,
	std::vector<uint32_t>&indices)
{
	uint32_t total = TILE_VERT_COUNT*static_cast<uint32_t>(tiles.size());

	verts.reserve(total);
	indices.reserve(6*total);

	static const uint32_t n = TILE_VERT_WIDTH;

	for (tile_code_t code : tiles) {

		uint8_t f = code.face;

		uint32_t idx = 0;

		aabb2_t rect = morton_u64_to_rect_f64(code.idx,code.zoom);

		uint32_t offset = static_cast<uint32_t>(verts.size());

		for (uint32_t i = 0; i < n; ++i) {
			double u = (double)i/(double)(n - 1);
			for (uint32_t j = 0; j < n; ++j) {
				double v = (double)j/(double)(n - 1);

				glm::dvec2 uv = glm::dvec2(u,v);
				glm::dvec2 face_uv = (glm::dvec2(1.0) - uv)*(rect.min) + uv*(rect.max);
				glm::dvec3 p = cube_to_globe(f, face_uv);

				vertex3d vert = {
					.postion = glm::vec3(p),
					.uv = glm::vec2(uv),
					.normal = glm::vec3(p)
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

}; // namespace globe
