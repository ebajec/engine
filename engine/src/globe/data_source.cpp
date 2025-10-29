#include "globe/data_source.h"

#include <type_traits>
#include <atomic>
#include <algorithm>

static float test_elev_fn(glm::dvec2 uv, uint8_t f);

static int test_loader_fn2(void *usr, uint64_t id, 
					struct ds_buf *buf, struct ds_token *token);
static_assert(std::is_same<decltype(&test_loader_fn2), ds_load_fn>::value);

TileDataSource 
*TileDataSource::create(ds_load_fn loader, void* usr)
{
	static std::atomic_uint64_t id_ctr = 0;

	TileDataSource *source = new TileDataSource;

	source->m_loader = loader ? loader : test_loader_fn2;
	source->m_usr = usr;
	source->m_id = ++id_ctr;

	return source;
}

TileCode TileDataSource::find(TileCode code) const
{
	auto end = m_data.end();
	auto it = m_data.find(code);

	while (code.zoom > m_debug_zoom && it == end) {
		code.idx >>= 2;
		--code.zoom;

		it = m_data.find(code);
	} 

	return (it == end) ? code : it->first;
}

float TileDataSource::sample_elevation_at(glm::dvec2 uv, uint8_t f) const
{
	return test_elev_fn(uv,f);
}

float TileDataSource::sample_elevation_at(glm::dvec3 p) const
{
	glm::dvec2 uv;
	uint8_t f;
	globe_to_cube(p, &uv, &f);

	return test_elev_fn(uv,f);
}

float TileDataSource::tile_min(TileCode code) const
{
	// Terrible - this is only temporary
	aabb2_t rect = morton_u64_to_rect_f64(code.idx,code.zoom);
	glm::dvec2 mid_uv = 0.5*(rect.ur() + rect.ll());

	return std::max({
         sample_elevation_at(rect.ll(),code.face),
         sample_elevation_at(rect.lr(),code.face),
         sample_elevation_at(rect.ul(),code.face),
         sample_elevation_at(rect.ur(),code.face),
         sample_elevation_at(mid_uv,code.face)   
	});
}
float TileDataSource::tile_max(TileCode code) const
{
	// Terrible - this is only temporary
	aabb2_t rect = morton_u64_to_rect_f64(code.idx,code.zoom);
	glm::dvec2 mid_uv = 0.5*(rect.ur() + rect.ll());

	return std::min({
         sample_elevation_at(rect.ll(),code.face),
         sample_elevation_at(rect.lr(),code.face),
         sample_elevation_at(rect.ul(),code.face),
         sample_elevation_at(rect.ur(),code.face),
         sample_elevation_at(mid_uv,code.face)   
	});

}

float TileDataSource::max() const
{
	return TileDataSource::TEST_AMP;
}
float TileDataSource::min() const
{
	return -TileDataSource::TEST_AMP;
}

//------------------------------------------------------------------------------
// Test loader functions

static constexpr size_t coeff_count = 4;
static double coeffs[coeff_count] = {};
static glm::dvec3 phases[coeff_count] = {};

static double urandf1()
{
	return (1.0 - (double)rand())/(double)RAND_MAX;
}

static void init_coeffs()
{
	for (size_t i = 0; i < coeff_count; ++i) {
		coeffs[i] = (1.0 - (double)rand())/(double)RAND_MAX; 
		phases[i] = glm::vec3(
			urandf1(),urandf1(),urandf1());
	}
}

static double test_elev_fn2(glm::dvec2 uv, uint8_t f, uint8_t zoom)
{
	glm::dvec3 p = cube_to_globe(f, uv);

	static int init = 0;

	if (!init) {
		++init;
		init_coeffs();
	}

	double g = 0;
	for (size_t i = 0; i < coeff_count; ++i) {
		double idx = (double)i + 1;
		double c = TileDataSource::TEST_FREQ*(idx);
		glm::dvec3 h = 0.5*TileDataSource::TEST_FREQ*phases[i]*TWOPI;
		g += (coeffs[i]/(double)idx)*(sin(c*p.x - h.x)*sin(c*p.y - h.y)*sin(c*p.z - h.z));
	}

	g *= TileDataSource::TEST_AMP;

	return g;
}

float test_elev_fn(glm::dvec2 uv, uint8_t f)
{
	return (float)test_elev_fn2(uv, f,coeff_count);
}

int test_loader_fn2(
	void *usr, uint64_t id, struct ds_buf *buf, struct ds_token *token)
{
	float *data = static_cast<float*>(buf->dst);

	TileCode code = tile_code_unpack(id);

	aabb2_t rect = morton_u64_to_rect_f64(code.idx, code.zoom);

	float d = 1.0f/(float)(TILE_WIDTH - 1);

	glm::vec2 uv = glm::vec2(0);
	size_t idx = 0;

	const struct ds_token_vtbl *vtbl = token->vtbl;
	for (size_t i = 0; i < TILE_WIDTH; ++i) {
		if (vtbl->is_cancelled(token))
			return 0;

		for (size_t j = 0; j < TILE_WIDTH; ++j) {
			glm::vec2 f = glm::mix(rect.ll(),rect.ur(),uv);

			double g = test_elev_fn2(f, code.face, code.zoom);
			data[idx++] = static_cast<float>(g);
			uv.x += d;
		}
		uv.x = 0;
		uv.y += d;
	}

	return 0;
}
