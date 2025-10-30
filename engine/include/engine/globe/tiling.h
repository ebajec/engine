#ifndef GLOBE_TILING_H
#define GLOBE_TILING_H

// local
#include "engine/utils/geometry.h"

// glm
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// libc
#include <cstdint>
#include <cfloat>

static constexpr uint32_t CUBE_FACES = 6;

static constexpr uint32_t TILE_WIDTH = 256;
static constexpr uint32_t TILE_SIZE = TILE_WIDTH*TILE_WIDTH;

//------------------------------------------------------------------------------
// TILE INDEXING

static constexpr uint64_t 	TILE_CODE_FACE_MASK = 0x7;
static constexpr uint8_t  	TILE_CODE_FACE_SHIFT = 0;
static constexpr uint64_t 	TILE_CODE_ZOOM_MASK = 0x1F;
static constexpr uint8_t  	TILE_CODE_ZOOM_SHIFT = 3;
static constexpr uint64_t 	TILE_CODE_IDX_MASK = 0x00FFFFFFFFFFFFFF;
static constexpr uint8_t 	TILE_CODE_IDX_SHIFT = 8;

typedef enum
{
	TILE_LOWER_LEFT = 0x0,
	TILE_LOWER_RIGHT = 0x1,
	TILE_UPPER_LEFT = 0x2,
	TILE_UPPER_RIGHT = 0x3
} tile_quadrant_t;

// NOTE: layout is not portable.  This is only to be used 
// for convenience.
struct alignas(8) TileCode
{
	uint8_t face : 3;
	uint8_t zoom : 5;
	uint64_t idx : 56;

	constexpr bool operator == (const TileCode& other) const {
		return face == other.face && zoom == other.zoom && idx == other.idx;
	}
};

static inline constexpr uint64_t tile_code_coarsen(uint64_t u64)
{
	uint8_t zoom = (uint8_t)((u64 >> TILE_CODE_ZOOM_SHIFT) & TILE_CODE_ZOOM_MASK);
	uint64_t idx  = (u64 >> TILE_CODE_IDX_SHIFT) & TILE_CODE_IDX_MASK;

	--zoom;
	idx >>= 2;

	// clear the old zoom and idx fields
	u64 &= ~(
		(TILE_CODE_IDX_MASK << TILE_CODE_IDX_SHIFT) | 
		(TILE_CODE_ZOOM_MASK << TILE_CODE_ZOOM_SHIFT)
	);

	u64 |= ((uint64_t)zoom << TILE_CODE_ZOOM_SHIFT); 
	u64 |= (idx << TILE_CODE_IDX_SHIFT); 

	return u64;
}

static constexpr uint64_t tile_code_refine(uint64_t u64, tile_quadrant_t quadrant)
{
	uint8_t zoom = (uint8_t)((u64 >> TILE_CODE_ZOOM_SHIFT) & TILE_CODE_ZOOM_MASK);
	uint64_t idx  = (u64 >> TILE_CODE_IDX_SHIFT) & TILE_CODE_IDX_MASK;

	++zoom;
	idx <<= 2;
	idx |= quadrant;

	// clear the old zoom and idx fields
	u64 &= ~(
		(TILE_CODE_IDX_MASK << TILE_CODE_IDX_SHIFT) | 
		(TILE_CODE_ZOOM_MASK << TILE_CODE_ZOOM_SHIFT)
	);

	u64 |= ((uint64_t)zoom << TILE_CODE_ZOOM_SHIFT); 
	u64 |= (idx << TILE_CODE_IDX_SHIFT); 

	return u64;
}

static constexpr TileCode tile_code_refine(TileCode code, tile_quadrant_t quadrant)
{
	++code.zoom;
	code.idx <<= 2;
	code.idx |= quadrant;
	return code;
}

static inline constexpr uint8_t tile_code_zoom(uint64_t u64)
{
	return (uint8_t)((u64 >> TILE_CODE_ZOOM_SHIFT) & TILE_CODE_ZOOM_MASK);
}

static inline constexpr uint64_t tile_code_idx(uint64_t u64)
{
	return (uint8_t)((u64 >> TILE_CODE_ZOOM_SHIFT) & TILE_CODE_ZOOM_MASK);
}

/// upper <- lower
/// | idx (56 bits) | zoom (5 bits) | face (3 bits) |
static inline constexpr TileCode tile_code_unpack(uint64_t u64)
{
	TileCode code;
	code.face = (uint8_t)((u64 >> TILE_CODE_FACE_SHIFT) & TILE_CODE_FACE_MASK);
	code.zoom = (uint8_t)((u64 >> TILE_CODE_ZOOM_SHIFT) & TILE_CODE_ZOOM_MASK);
	code.idx  = (u64 >> TILE_CODE_IDX_SHIFT) & TILE_CODE_IDX_MASK;

	return code;
}

/// upper <- lower
/// | idx (56 bits) | zoom (5 bits) | face (3 bits) |
static inline constexpr uint64_t tile_code_pack(TileCode code) {
	uint64_t u64 = 0;
	u64 |= ((uint64_t)code.face << TILE_CODE_FACE_SHIFT); 
	u64 |= ((uint64_t)code.zoom << TILE_CODE_ZOOM_SHIFT); 
	u64 |= (code.idx << TILE_CODE_IDX_SHIFT); 

	return u64;
}

/// upper <- lower
/// | idx (56 bits) | zoom (5 bits) | face (3 bits) |
static inline uint64_t tile_code_pack2(uint8_t face, uint8_t zoom, uint64_t idx) {
	uint64_t u64 = 0;
	u64 |= ((uint64_t)face << TILE_CODE_FACE_SHIFT); 
	u64 |= ((uint64_t)zoom << TILE_CODE_ZOOM_SHIFT); 
	u64 |= (idx << TILE_CODE_IDX_SHIFT); 
	return u64;
}

static constexpr TileCode TILE_CODE_NONE = tile_code_unpack(UINT64_MAX);

static_assert(tile_code_pack(tile_code_unpack(0x8493724890123809)) == 0x8493724890123809); 
static_assert(tile_code_pack(tile_code_unpack(0x020)) == 0x020); 


struct TileCodeHash
{
	size_t operator()(const TileCode& code) const {
		return std::hash<uint64_t>{}(tile_code_pack(code));
	}
};

//------------------------------------------------------------------------------
// TRANSFORMS

static inline uint8_t cube_face(glm::dvec3 v)
{
	double c[6] = {v.x, v.y, v.z, -v.x, -v.y, -v.z};

	uint32_t argmax = 0;
	double max = -DBL_MAX;

	for (uint32_t i = 0; i < 6; ++i) {
		if (c[i] >= max) {
			argmax = i;
			max = c[i];
		}
	}

	return (uint8_t)argmax;
}

template<typename T>
static inline glm::vec<3, T, glm::defaultp> 
world_to_face(glm::vec<3, T, glm::defaultp> v, unsigned int face)
{
	using _ty = glm::vec<3, T, glm::defaultp>;

	switch (face) {
		case 0: return _ty(v.y, v.z, v.x);
		case 1: return _ty(-v.x, v.z, v.y);
		case 2: return _ty(v.y, -v.x, v.z);
		case 3: return _ty(v.y, -v.z, -v.x);
		case 4: return _ty(v.x, v.z, -v.y);
		case 5: return _ty(v.y, v.x, -v.z);
	} 
	return _ty(0);
}
template<typename T>
static inline glm::vec<3, T, glm::defaultp> 
face_to_world(glm::vec<3, T, glm::defaultp> v, unsigned int face)
{
	using _ty = glm::vec<3, T, glm::defaultp>;

	switch (face) {
		case 0: return _ty(v.z, v.x, v.y);
		case 1: return _ty(-v.x, v.z, v.y);
		case 2: return _ty(-v.y, v.x, v.z);
		case 3: return _ty(-v.z, v.x, -v.y);
		case 4: return _ty(v.x, -v.z, v.y);
		case 5: return _ty(v.y, v.x, -v.z);
	} 
	return _ty(0);
}

static inline glm::dvec2 gnomic_proj_cube_face(glm::dvec3 p, uint8_t f)
{
	p = world_to_face(p,f);
	p /= copysign(std::max(fabs(p.z),1e-14),p.z);
	glm::dvec2 uv = 0.5 * (glm::dvec2(1.0) + glm::dvec2(p.x,p.y));

	return uv;
}

static inline void globe_to_cube(glm::dvec3 p, glm::dvec2 *p_uv, uint8_t *p_f)
{
	uint8_t f = cube_face(p);
	*p_uv = gnomic_proj_cube_face(p,f);
	*p_f = f;
}

static inline glm::dvec3 cube_to_globe(uint8_t face, glm::dvec2 uv) 
{
	glm::dvec3 c = glm::vec3((2.0*uv - glm::dvec2(1.0)),1);
	return glm::normalize(face_to_world(c, face));
}

static inline glm::dmat3 orthonormal_globe_frame(glm::dvec2 _uv, uint8_t face)
{
	_uv = 2.0*_uv - glm::dvec2(2.0);

	double u = _uv.x;
	double v = _uv.y;

	double uu = u*u;
	double vv = v*v;
	double uv = u*v;

	double r_inv = 1.0/sqrt(1.0 + uu + vv);
	double nu_inv = 1.0/sqrt(1.0 + uu);
	double nv_inv = 1.0/sqrt(1.0 + vv);

	glm::dvec3 Tu = glm::dvec3(1.0 + vv, -uv, -u)*r_inv*nv_inv;
	glm::dvec3 Tv = glm::dvec3(-uv, 1.0 + uu, -v)*r_inv*nu_inv;

	glm::dvec3 Mu = glm::dvec3(1, 0, -u)*nu_inv;
	glm::dvec3 Mv = glm::dvec3(0, 1, -v)*nv_inv;

	glm::dvec3 T = glm::normalize(Tu + Mu);
	glm::dvec3 B = glm::normalize(Tv + Mv);

	// T and B are already orthonormal
	glm::dvec3 N = cross(T,B);

	return glm::dmat3(
		face_to_world(T,face),
		face_to_world(B,face),
		face_to_world(N,face)
	);
}

static inline glm::dmat3 tile_frame(TileCode code)
{
	aabb2_t rect = morton_u64_to_rect_f64(code.idx, code.zoom);
	
	glm::dvec2 mid = 0.5*(rect.ll() + rect.ur());

	return orthonormal_globe_frame(mid, code.face);
}

static inline TileCode tile_cell_index(TileCode code)
{
	uint8_t cell_zoom = code.zoom/8;
	uint32_t shift = 16*cell_zoom;
	return TileCode {
		.face = code.face,
		.zoom = static_cast<uint8_t>(8*cell_zoom),
		.idx = code.idx >> shift
	};
}

static inline TileCode tile_encode(uint8_t zoom, glm::dvec3 p)
{
	assert(zoom < 24);
	uint8_t f;
	glm::dvec2 uv; 

	globe_to_cube(p, &uv, &f);

	TileCode code = {
		.face = f,
		.zoom = zoom,
		.idx = morton_u64(uv.x,uv.y,zoom)
	};

	return code; 
}

// very rough approximation used to estimate screen error for 
// a tile. Improve this.
static inline constexpr double tile_factor(uint8_t lvl)
{
	// TODO : Compute the actual surface integral (or approximate with
	// a series to an acceptable order).
	//
	// This is fully incorrect
	return (4.0 * PI / 6.0)/(double)(1LU << 2*lvl);
}

#endif // GLOBE_TILING_H

