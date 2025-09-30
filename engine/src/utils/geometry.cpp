#include "engine/utils/geometry.h"

#include <complex>

#include <cstdint>

void geometry::mesh_s2(uint32_t ntht, uint32_t nphi, 
					   std::vector<vertex3d>& verts, 
					   std::vector<uint32_t>& indices
					   )
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

void geometry::mesh_torus(float R1, float R2, uint32_t ntht1, uint32_t ntht2,  
						  std::vector<vertex3d>& verts, 
						  std::vector<uint32_t>& indices
						  )
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

void geometry::mesh_cube_map(float scale, uint32_t nx, uint32_t ny,
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

