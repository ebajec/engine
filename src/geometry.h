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
				.postion = p,
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
				.postion = p,
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

}; // namespace geometry


#endif
