#ifndef GLOBE_H
#define GLOBE_H

#include "geometry.h"

#include <cfloat>

namespace globe
{
	struct tile_code_t
	{
		uint8_t face : 3;
		uint8_t zoom : 5;
		uint64_t idx : 56;
	};

	extern void select_tiles(
		std::vector<tile_code_t>& tiles,
		const frustum_t& frust, 
		glm::dvec3 center, 
		double res
	);

	extern void create_mesh(
		double scale,
		glm::dvec3 origin,
		const std::vector<tile_code_t>& tiles, 
		std::vector<vertex3d>& verts,
		std::vector<uint32_t>&indices
	);

	static constexpr glm::dmat3 g_faces_d[] = {
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
	static constexpr glm::mat3 g_faces_f[] = {
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

	static inline constexpr double tile_area(uint8_t lvl)
	{
		return (4.0 * PI / 6.0)/(double)(1LU << 2*lvl);
	}

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

		return argmax;
	}

	static inline constexpr glm::dvec2 globe_to_cube_face(glm::dvec3 p, uint8_t f)
	{
		glm::dmat3 m = glm::transpose(g_faces_d[f]);

		p = m*p;
		p /= p.z;

		glm::dvec2 uv = 0.5 * (glm::dvec2(1.0) + glm::dvec2(p.x,p.y));

		return uv;
	}

	static inline constexpr void globe_to_cube(glm::dvec3 p, glm::dvec2 *p_uv, uint8_t *p_f)
	{
		uint8_t f = cube_face(p);
		*p_uv = globe_to_cube_face(p,f);
		*p_f = f;
	}

	static inline constexpr glm::dvec3 cube_to_globe(uint8_t face, glm::dvec2 uv) 
	{
		glm::dmat3 m = g_faces_d[face];
		glm::vec3 p = m[2] + glm::dmat2x3(m[0],m[1])*(2.0*uv - glm::dvec2(1.0)); 
		return p/length(p);
	}

	static inline constexpr tile_code_t encode(uint8_t zoom, glm::dvec3 p)
	{
		assert(zoom < 24);
		uint8_t f;
		glm::dvec2 uv; 

		globe_to_cube(p, &uv, &f);

		tile_code_t code = {
			.face = f,
			.zoom = zoom,
			.idx = morton_u64(uv.x,uv.y,zoom)
		};

		return code; 
	}
};

#endif
