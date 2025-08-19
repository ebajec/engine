#ifndef TILING_H
#define TILING_H

// local
#include "geometry.h"

// glm
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// libc
#include <cstdint>
#include <cfloat>

static constexpr uint32_t CUBE_FACES = 6;

union TileCode
{
	struct {
		uint8_t face : 3;
		uint8_t zoom : 5;
		uint64_t idx : 56;
	};
	uint64_t u64;

	constexpr bool operator == (const TileCode& other) const {
		return u64 == other.u64;
	}
};
static constexpr TileCode TILE_CODE_NONE = {.u64 = 0xFFFFFFFFFFFFFFFF};

struct TileCodeHash
{
	constexpr size_t operator()(const TileCode& code) const {
		return std::hash<uint64_t>{}(code.u64);
	}
};

static inline constexpr uint8_t cube_face(glm::dvec3 v)
{
	double c[6] = {v.x,v.y,v.z,-v.x,-v.y,-v.z};

	uint argmax = 0;
	double max = -DBL_MAX;

	for (uint i = 0; i < 6; ++i) {
		if (c[i] >= max) {
			argmax = i;
			max = c[i];
		}
	}

	return (uint8_t)argmax;
}

static inline glm::vec3 world_to_face(glm::vec3 v, uint face)
{
	switch (face) {
	case 0:
		return glm::vec3(v.y,v.z,v.x);
	case 1:
		return glm::vec3(-v.x,v.z,v.y);
	case 2:
		return glm::vec3(v.y,-v.x,v.z);
	case 3:
		return glm::vec3(v.y,-v.z,-v.x);
	case 4:
		return glm::vec3(v.x,v.z,-v.y);
	case 5:
		return glm::vec3(v.y,v.x,-v.z);
	} 
	return glm::vec3(0);
}

static inline glm::vec3 face_to_world(glm::vec3 v, uint face)
{
	switch (face) {
	case 0:
		return glm::vec3(v.z,v.x,v.y);
	case 1:
		return glm::vec3(-v.x,v.z,v.y);
	case 2:
		return glm::vec3(-v.y,v.x,v.z);
	case 3:
		return glm::vec3(-v.z,v.x,-v.y);
	case 4:
		return glm::vec3(v.x,-v.z,v.y);
	case 5:
		return glm::vec3(v.y,v.x,-v.z);
	} 
	return glm::vec3(0);
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

static TileCode tile_cell_index(TileCode code)
{
	uint8_t cell_zoom = code.zoom/8;
	uint32_t shift = 16*cell_zoom;
	return TileCode {
		.zoom = cell_zoom,
		.face = code.face,
		.idx = code.idx >> shift
	};
}

static inline constexpr TileCode tile_encode(uint8_t zoom, glm::dvec3 p)
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

static inline constexpr double tile_area(uint8_t lvl)
{
	// TODO : Compute the actual surface integral (or approximate with
	// a series to an acceptable order).
	return (4.0 * PI / 6.0)/(double)(1LU << 2*lvl);
}

inline constexpr glm::dmat3 cube_face_basis_d(uint8_t face)
{
	switch (face) {
	case 0:
	return glm::transpose(glm::dmat3( // y ^ z
		0,0,1,
		1,0,0,
		0,1,0
	)); 
	case 1:
	return glm::transpose(glm::dmat3( // x ^ z
		-1,0,0,
		0,0,1,
		0,1,0
	)); 
	case 2:
	return glm::transpose(glm::dmat3( // x ^ y
		0,-1,0,
		1,0,0,
		0,0,1
	)); 
	case 3:
	return glm::transpose(glm::dmat3( // y ^ -z
		0,0,-1,
		1,0,0,
		0,-1,0
	)); 
	case 4:
	return glm::transpose(glm::dmat3( // - x ^ z
		1,0,0,
		0,0,-1,
		0,1,0
	)); 
	case 5:
	return glm::transpose(glm::dmat3( // - x ^ x
		0,1,0,
		1,0,0,
		0,0,-1
	));
	}
	return glm::mat3(0);
};

#endif //TILING_H
