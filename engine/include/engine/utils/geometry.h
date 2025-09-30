#ifndef GEOMETRY_H
#define GEOMETRY_H

#include "engine/renderer/types.h"

// glm
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

// STL
#include <vector>

// libc
#include <cstdint>
#include <cmath>

#ifndef PI
#define PI 3.141592654
#endif 

#ifndef TWOPI
#define TWOPI (2.0*3.141592654)
#endif

#ifndef TWOPIf
#define TWOPIf (2.0f*3.141592654f)
#endif

struct plane_t
{
	glm::dvec3 n;
	double d;
};

struct aabb2_t
{
	glm::dvec2 min, max;

	constexpr inline glm::dvec2 ll() const {return min;}
	constexpr inline glm::dvec2 ur() const {return max;}
	constexpr inline glm::dvec2 ul() const {return {min.x,max.y};}
	constexpr inline glm::dvec2 lr() const {return {max.x,min.y};}
};

struct aabb3_t
{
	glm::dvec3 min, max;
};

struct circle_t
{
	glm::dvec2 c;
	double r;
};

struct obb_t
{
	glm::dmat3 T; // transform
	glm::dvec3 c; // center
	glm::dvec3 limits;
};

enum frustum_planes_t
{
	FRUSTUM_PLANE_LEFT,
	FRUSTUM_PLANE_RIGHT,
	FRUSTUM_PLANE_DOWN,
	FRUSTUM_PLANE_UP,
	FRUSTUM_PLANE_NEAR,
	FRUSTUM_PLANE_FAR,
};

union frustum_t
{
	struct {
		plane_t left, right, down, up, near, far;
	} p;
	plane_t planes[6];
};

//------------------------------------------------------------------------------
// AABB

/// @return 1 if box is entirely in front, -1 if entirely behind, 0 otherwise
static inline int classify(const aabb3_t& box, const plane_t& pl)
{ 
	glm::dvec3 c = (box.min + box.max) * 0.5;
	glm::dvec3 e = (box.max - box.min) * 0.5;

    double r = fabs(e.x*pl.n.x) + fabs(e.y*pl.n.y) + fabs(e.z*pl.n.z);

    double s = dot(pl.n, c) - pl.d;

    if (s > r)   return 1;  // entirely in front
    if (s < -r)  return -1;  // entirely behind
    return 0;                // intersects
}

static constexpr bool aabb2_contains(const aabb2_t &box, glm::dvec2 v)
{
	return box.min.x <= v.x || v.x <= box.max.x || box.min.y <= v.y || v.y <= box.max.y;
}

static inline double aabb3_dist_sq(const aabb3_t& box, glm::dvec3 v)
{
	glm::dvec3 d = glm::dvec3(0);

	if (v.x < box.min.x)
		d.x = box.min.x - v.x;
	else if (v.x > box.max.x)
		d.x = v.x - box.max.x;

	if (v.y < box.min.y)
		d.y = box.min.y - v.y;
	else if (v.y > box.max.y)
		d.y = v.y - box.max.y;

	if (v.z < box.min.z)
		d.z = box.min.z - v.z;
	else if (v.z > box.max.z)
		d.z = v.z - box.max.z;

	return dot(d,d);
}

static inline bool intersects(const aabb2_t& box, const circle_t &circ)
{
	glm::dvec2 nearest = glm::clamp(circ.c, box.ll(),box.ur());
	glm::dvec2 d = circ.c - nearest;
    return dot(d,d) < circ.r*circ.r;
}

static inline bool intersects(const aabb3_t& b1, const aabb3_t& b2)
{
	return !(
		b1.max.x < b2.min.x || b2.max.x < b1.min.x ||
		b1.max.y < b2.min.y || b2.max.y < b1.min.y ||
		b1.max.z < b2.min.z || b2.max.z < b1.min.z 
	);
}

static inline aabb3_t aabb3_add(aabb3_t box, glm::dvec3 p)
{
	box.max = glm::max(p,box.max);
	box.min = glm::min(p,box.min);
	return box;
}

static inline aabb3_t aabb3_bounding(const glm::dvec3 *pts, size_t count)
{
	assert(count);

	aabb3_t box;

	box.min = box.max = pts[0];

	for (size_t i = 1; i < count; ++i) {
		glm::dvec3 p = pts[i];
		box.min = glm::min(box.min,p);
		box.max = glm::max(box.max,p);
	}

	return box;
}

//------------------------------------------------------------------------------
// FRUSTUM

static inline frustum_t camera_frustum(glm::dmat4 m)
{
	m = transpose(m);
	frustum_t frust;
	for (int i = 0; i < 6; ++i) {
		glm::dvec4 p;
		switch (i) {
			case FRUSTUM_PLANE_LEFT:  p = m[3] + m[0]; break; // left
			case FRUSTUM_PLANE_RIGHT: p = m[3] - m[0]; break; // right
			case FRUSTUM_PLANE_DOWN:  p = m[3] + m[1]; break; // bottom
			case FRUSTUM_PLANE_UP:    p = m[3] - m[1]; break; // top
			case FRUSTUM_PLANE_NEAR:  p = m[3] + m[2]; break; // near
			case FRUSTUM_PLANE_FAR:   p = m[3] - m[2]; break; // far
		}

		double r_inv = 1.0 / length(glm::dvec3(p));
		frust.planes[i].n = -glm::dvec3(p) * r_inv;
		frust.planes[i].d = p.w * r_inv;
	}
	
	return frust;
}

static inline aabb3_t frustum_aabb(const frustum_t& frust)
{
	// TODO : make this less hacky (we probably don't need to solve 4 linear systems?) 
	glm::dmat3 m1 = glm::transpose(glm::dmat3(frust.p.far.n,frust.p.right.n,frust.p.up.n));
	glm::dmat3 m2 = glm::transpose(glm::dmat3(frust.p.far.n,frust.p.left.n,frust.p.up.n));
	glm::dmat3 m3 = glm::transpose(glm::dmat3(frust.p.far.n,frust.p.right.n,frust.p.down.n));
	glm::dmat3 m4 = glm::transpose(glm::dmat3(frust.p.far.n,frust.p.left.n,frust.p.down.n));

	glm::dvec3 p[8] = {
		glm::inverse(m1) * glm::dvec3(frust.p.far.d,frust.p.right.d,frust.p.up.d),
		glm::inverse(m2) * glm::dvec3(frust.p.far.d,frust.p.left.d,frust.p.up.d),
		glm::inverse(m3) * glm::dvec3(frust.p.far.d,frust.p.right.d,frust.p.down.d),	
		glm::inverse(m4) * glm::dvec3(frust.p.far.d,frust.p.left.d,frust.p.down.d)	
	};

	glm::dvec3 back = frust.p.far.n * (frust.p.far.d + frust.p.near.d);

	p[4] = p[0] - back;
	p[5] = p[1] - back;
	p[6] = p[2] - back;
	p[7] = p[3] - back;

	return aabb3_bounding(p, 8);
}

/// @return true if v is behind, false otherwise 
static inline bool cull_plane(const plane_t &p, const glm::dvec3 &v)
{
	return glm::dot(v,p.n) < p.d;
}

static inline bool within_frustum(glm::dvec3 v, const frustum_t &frust)
{
	for (uint32_t i = 0; i < 6; ++i) {
		if (!cull_plane(frust.planes[i],v))
			return false;
	}
	return true;
}

//------------------------------------------------------------------------------
// Spatial indexing

/// @brief Computes the hilbert curve index for a point (x,y) in [0,1] x [0,1]
/// @param n - Order of the index.  The number of bits computed for the code is 
/// 2*n
template <typename uint_t, float_t>
static inline uint_t hilbert_index_u32_f32(float_t xf, float_t yf, int n)
{
	uint_t N = (1 << n);
	uint_t mask = N - 1;
	float_t Nf = (float_t)N;
	uint_t x = ((uint_t)(xf*Nf)) & mask;
	uint_t y = ((uint_t)(yf*Nf)) & mask;

	uint_t code = 0x0;
	for (int i = n - 1; i >= 0; --i) {
		uint_t xi = ((x >> i) & 0x1);
		uint_t yi = ((y >> i) & 0x1);

		uint_t b = (0x3 * xi) ^ (yi);

		code = (code << 2) | b;

		uint_t nx = yi ? x : (xi ? y ^ mask : y); 
		uint_t ny = yi ? y : (xi ? x ^ mask : x); 
		
		x = nx;
		y = ny;
	}

	return code;
}

static inline constexpr uint64_t morton_u64(double x, double y, int level)
{
    double w = (double)(0x1 << level);

    uint64_t px = (uint64_t)(x*w);
    uint64_t py = (uint64_t)(y*w);

    uint64_t xi, yi; 

	uint64_t index = 0;

    while(level--) {
		uint64_t mask = 0x1 << level;

		xi = px & mask;
		yi = py & mask;

		index <<= 1;
		index |= yi >> level;
		index <<= 1;
		index |= xi >> level;
	}

    return index;
}

static constexpr aabb2_t morton_u64_to_rect_f64(uint64_t index, uint8_t level)
{
    aabb2_t box = {};

	double h = 1.0/(double)(1 << level);

	for (; level > 0; --level) {
        uint8_t xi = index & 0x1;
        index >>= 1;
        uint8_t yi = index & 0x1;
        index >>= 1;

		double hi = 1.0/(double)(1 << level);

        box.min.x += xi ? hi : 0.0;
        box.min.y += yi ? hi : 0.0;
	}

	box.max.x = box.min.x + h;
	box.max.y = box.min.y + h;

    return box;
}


namespace geometry
{
	extern void mesh_torus(float R1, float R2, uint32_t ntht1, uint32_t ntht2, 
				std::vector<vertex3d>& verts, std::vector<uint32_t>& indices);
	extern void mesh_s2(uint32_t ntht, uint32_t nphi, 
					 std::vector<vertex3d>& verts, 
					 std::vector<uint32_t>& indices);
	extern void mesh_cube_map(float scale, uint32_t nx, uint32_t ny,
				std::vector<vertex3d>& verts, std::vector<uint32_t>& indices);

}; // namespace geometry


#endif // GEOMETRY_H
