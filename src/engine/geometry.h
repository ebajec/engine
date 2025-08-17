#ifndef GEOMETRY_H
#define GEOMETRY_H

#include "def_gl.h"

#include <cstdint>
#include <cmath>
#include <vector>
#include <complex.h>

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
	union {
		struct {
			double x0,y0,x1,y1;
		};	
		struct {
			glm::dvec2 min, max;
		};
	};

	constexpr inline glm::dvec2 ll() const {return {x0,y0};}
	constexpr inline glm::dvec2 ur() const {return {x1,y1};}
	constexpr inline glm::dvec2 ul() const {return {x0,y1};}
	constexpr inline glm::dvec2 lr() const {return {x1,y0};}
};

struct aabb3_t
{
	union{
		struct {
			double x0,y0,z0, x1,y1,z1;
		};	
		struct {
			glm::dvec3 min, max;
		};
	};
};

struct circle_t
{
	glm::dvec2 c;
	double r;
};

struct obb_t
{
	glm::dmat4 T;
	glm::dvec2 limits;
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

struct frustum_t
{
	union {
		struct {
			plane_t left, right, down, up, near, far;
		};
		plane_t planes[6];
	};
};

static inline aabb3_t aabb_bounding(glm::dvec3 *pts, size_t count);

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

static inline aabb3_t frust_cull_box(const frustum_t& frust)
{
	// TODO : make this less hacky (we probably don't need to solve 4 linear systems?) 
	glm::dmat3 m1 = glm::transpose(glm::dmat3(frust.far.n,frust.right.n,frust.up.n));
	glm::dmat3 m2 = glm::transpose(glm::dmat3(frust.far.n,frust.left.n,frust.up.n));
	glm::dmat3 m3 = glm::transpose(glm::dmat3(frust.far.n,frust.right.n,frust.down.n));
	glm::dmat3 m4 = glm::transpose(glm::dmat3(frust.far.n,frust.left.n,frust.down.n));

	glm::dvec3 p[8] = {
		glm::inverse(m1) * glm::dvec3(frust.far.d,frust.right.d,frust.up.d),
		glm::inverse(m2) * glm::dvec3(frust.far.d,frust.left.d,frust.up.d),
		glm::inverse(m3) * glm::dvec3(frust.far.d,frust.right.d,frust.down.d),	
		glm::inverse(m4) * glm::dvec3(frust.far.d,frust.left.d,frust.down.d)	
	};

	glm::dvec3 back = frust.far.n * (frust.far.d + frust.near.d);

	p[4] = p[0] - back;
	p[5] = p[1] - back;
	p[6] = p[2] - back;
	p[7] = p[3] - back;

	return aabb_bounding(p, 8);
}

static constexpr bool aabb_contains(aabb2_t box, glm::dvec2 v)
{
	return box.x0 <= v.x || v.x <= box.x1 || box.y0 <= v.y || v.y <= box.y1;
}

static inline double aabb3_dist_sq(const aabb3_t& box, glm::dvec3 v)
{
	glm::dvec3 d = glm::dvec3(0);

	if (v.x < box.x0)
		d.x = box.x0 - v.x;
	else if (v.x > box.x1)
		d.x = v.x - box.x1;

	if (v.y < box.y0)
		d.y = box.y0 - v.y;
	else if (v.y > box.y1)
		d.y = v.y - box.y1;

	if (v.z < box.z0)
		d.z = box.z0 - v.z;
	else if (v.z > box.z1)
		d.z = v.z - box.z1;

	return dot(d,d);
}


static constexpr bool cull_plane(plane_t p, glm::dvec3 v)
{
	return glm::dot(v,p.n) < p.d;
}

static inline bool within_frustum(glm::dvec3 v, frustum_t frust)
{
	for (uint i = 0; i < 6; ++i) {
		if (!cull_plane(frust.planes[i],v))
			return false;
	}
	return true;
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
		b1.x1 < b2.x0 || b2.x1 < b1.x0 ||
		b1.y1 < b2.y0 || b2.y1 < b1.y0 ||
		b1.z1 < b2.z0 || b2.z1 < b1.z0 
	);
}

static inline int classify(const aabb3_t& box, const plane_t& pl)
{ 
	glm::dvec3 c = (box.min + box.max) * 0.5;
	glm::dvec3 e = (box.max - box.min) * 0.5;

    double r = fabs(e.x*pl.n.x) + fabs(e.y*pl.n.y) + fabs(e.z*pl.n.z);

    double s = dot(pl.n, c) - pl.d;

    if (s > r)   return +1;  // entirely in front
    if (s < -r)  return -1;  // entirely behind
    return 0;                // intersects
}

static inline aabb3_t aabb_bounding(glm::dvec3 *pts, size_t count)
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

static inline aabb3_t aabb_add(aabb3_t box, glm::dvec3 p)
{
	box.max = glm::max(p,box.max);
	box.min = glm::min(p,box.min);
	return box;
}

static inline constexpr uint64_t morton_u64(double x, double y, uint8_t level)
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
    aabb2_t extent = {
		.min = glm::dvec2(0),
		.max = glm::dvec2(0)
	};

	double h = 1.0/(double)(1 << level);

	for (; level > 0; --level) {
        uint8_t xi = index & 0x1;
        index >>= 1;
        uint8_t yi = index & 0x1;
        index >>= 1;

		double hi = 1.0/(double)(1 << level);

        extent.x0 += xi ? hi : 0.0;
        extent.y0 += yi ? hi : 0.0;
	}

	extent.x1 = extent.x0 + h;
	extent.y1 = extent.y0 + h;

    return extent;
}


namespace geometry
{

static inline void mesh_s2(uint32_t ntht, uint32_t nphi, std::vector<vertex3d>& verts, std::vector<uint32_t>& indices)
{
	double tht = 0;

	double tht_max = TWOPI;

	double dphi = PI/(nphi - 1);
	double dtht = tht_max/(ntht - 1);

	verts.reserve(nphi*ntht);
	indices.resize(6*nphi*ntht);

	uint32_t idx = 0;

	for (uint32_t i = 0; i < ntht; ++i) {
		double sin_tht = sin(tht);
		double cos_tht = cos(tht);

		uint32_t in = i + 1 < ntht ? i + 1 : 0; 

		double phi = 0;
		for (uint32_t j = 0; j < nphi; ++j) {

			double sin_phi = sin(phi);
			glm::vec3 p = glm::vec3(sin_phi*cos_tht,sin_phi*sin_tht,-cos(phi));
			glm::vec3 n = p;
			glm::vec2 uv = glm::vec2(tht/tht_max,phi/PI);

			verts.push_back(vertex3d{
				.position = p,
				.uv = uv,
				.normal = n
			});

			uint32_t jn = j + 1 < nphi ? j + 1 : j; 

			indices[idx++] = i*nphi + j;
			indices[idx++] = in*nphi + j;
			indices[idx++] = in*nphi + jn;

			indices[idx++] = i*nphi + j;
			indices[idx++] = in*nphi + jn;
			indices[idx++] = i*nphi + jn;

			phi += dphi;
		}
		tht += dtht;
	}
}

static inline void mesh_torus(float R1, float R2, uint32_t ntht1, uint32_t ntht2, 
				std::vector<vertex3d>& verts, std::vector<uint32_t>& indices)
{
	using namespace std::complex_literals;

	size_t count = ntht1*ntht2;

	verts.reserve(count);
	indices.resize(6*count);

	float du = 1.0f/(float)(ntht1-1);
	float dv = 1.0f/(float)(ntht2-1);

	std::complex<float> dc1 = std::polar<float>(1,TWOPIf*du);
	std::complex<float> dc2 = std::polar<float>(1,TWOPIf*dv);

	size_t idx = 0;

	float u = 0;
	std::complex<float> c1 = 1.f;

	for (uint32_t i = 0; i < ntht1; ++i) {
		glm::vec3 A = glm::vec3(c1.real(),c1.imag(),0);

		uint32_t in = i + 1 < ntht1 ? i + 1 : 0; 

		float v = 0;
		std::complex<float> c2 = 1.f;

		for (uint32_t j = 0; j < ntht2; ++j) {
			glm::vec3 B = glm::vec3(0,0,1)*c2.imag() - A*c2.real();

			glm::vec3 p = R1*A + R2*B;

			verts.push_back({
				.position = p,
				.uv = glm::vec2(u,v),
				.normal = glm::vec3(0)
			});

			uint32_t jn = j + 1 < ntht2 ? j + 1 : 0; 

			indices[idx++] = i*ntht2 + j;
			indices[idx++] = in*ntht2 + jn;
			indices[idx++] = in*ntht2 + j;

			indices[idx++] = i*ntht2 + j;
			indices[idx++] = i*ntht2 + jn;
			indices[idx++] = in*ntht2 + jn;

			c2 *= dc2;
			v += dv;
		}
		c1 *= dc1;
		u += du;
	}

}

static inline void mesh_cube_map(float scale, uint32_t nx, uint32_t ny,
				std::vector<vertex3d>& verts, std::vector<uint32_t>& indices)
{
	static constexpr uint32_t CUBE_FACES = 6;
	static constexpr glm::mat3 faces[] = {
		glm::mat3( // y ^ z
			0,1,0,
			0,0,1,
			1,0,0
		),
		glm::mat3( // x ^ z
			1,0,0,
			0,0,1,
			0,1,0
		),
		glm::mat3( // x ^ y
			1,0,0,
			0,1,0,
			0,0,1
		),
		glm::mat3( // -y ^ z
			0,-1,0,
			0,0,1,
			-1,0,0
		),
		glm::mat3( // - x ^ z
			-1,0,0,
			0,0,1,
			0,-1,0
		),
		glm::mat3( // - x ^ x
			-1,0,0,
			0,1,0,
			0,0,-1
		),
	};

	size_t pt_count = 6*((nx + 1)*(ny + 1));

	std::vector<glm::vec3> pts;
	std::vector<glm::vec2> uvs;

	for (uint32_t f = 0; f < CUBE_FACES; ++f) {
		glm::mat3 m = faces[f];

		for (uint32_t i = 0; i < nx + 1; ++i) {
			float u = (float)i/(float)nx;
			glm::vec3 pu = (2.f*u - 1.f)*m[0]; 

			for (uint32_t j = 0; j < ny + 1; ++j) {
				float v = (float)j/(float)ny;
				glm::vec3 pv = (2.f*v - 1.f)*m[1]; 
				glm::vec3 p = m[2] + pu + pv;

				pts.push_back(p);
				uvs.push_back(glm::vec2(u,v));
			}
		}
	}

	verts.reserve(pt_count);

	for (size_t i = 0; i < pts.size(); ++i) {
		verts.push_back({
			.position = scale * pts[i]/glm::length(pts[i]),
			.uv = uvs[i],
			.normal = glm::vec3(0),
	  	});
	}

	indices.resize(6 * pt_count);

	uint32_t idx = 0;
	for (uint32_t f = 0; f < CUBE_FACES; ++f) {
		uint32_t offset = f*(nx + 1)*(ny + 1);
		for (uint32_t i = 0; i < nx + 1; ++i) {
			for (uint32_t j = 0; j < ny + 1; ++j) {

				uint32_t in = std::min(i + 1,nx);
				uint32_t jn = std::min(j + 1,ny);

				indices[idx++] = offset + (ny + 1) * i + j; 
				indices[idx++] = offset + (ny + 1) * in + j; 
				indices[idx++] = offset + (ny + 1) * in + jn; 

				indices[idx++] = offset + (ny + 1) * i + j;
				indices[idx++] = offset + (ny + 1) * in + jn;
				indices[idx++] = offset + (ny + 1) * i + jn;
			}
		}
	}
}

}; // namespace geometry


#endif // GEOMETRY_H
