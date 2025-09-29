#include "globe/data_source.h"

static void test_loader_fn(TileCode code, void *dst, void *usr,
						   const pct_atomic_state *p_state);
static float test_elev_fn(glm::dvec2 uv, uint8_t f);


TileDataSource 
*TileDataSource::create(TileLoaderFunc loader, void* usr)
{
	TileDataSource *source = new TileDataSource;

	source->m_loader = loader ? loader : test_loader_fn;
	source->m_usr = usr;

	return source;
}

void TileDataSource::load(TileCode code, void* dst) const
{
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

//------------------------------------------------------------------------------
// Test loader functions

static constexpr size_t coeff_count = 5;
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
		double c = DATA_SOURCE_TEST_FREQ*(idx);
		glm::dvec3 h = 0.5*DATA_SOURCE_TEST_FREQ*phases[i]*TWOPI;
		g += (coeffs[i]/(double)idx)*(sin(c*p.x - h.x)*sin(c*p.y - h.y)*sin(c*p.z - h.z));
	}

	g *= DATA_SOURCE_TEST_AMP;

	return g;
}

float test_elev_fn(glm::dvec2 uv, uint8_t f)
{
	return (float)test_elev_fn2(uv, f,coeff_count);
}

void test_loader_fn(TileCode code, void *dst, void *usr, 
					const pct_atomic_state *p_state)
{
	float *data = static_cast<float*>(dst);

	aabb2_t rect = morton_u64_to_rect_f64(code.idx, code.zoom);

	float d = 1.0f/(float)(TILE_WIDTH - 1);

	glm::vec2 uv = glm::vec2(0);
	size_t idx = 0;
	for (size_t i = 0; i < TILE_WIDTH; ++i) {
		if (pct_state_status(p_state->load()) == PCT_STATUS_CANCELLED)
			return;

		for (size_t j = 0; j < TILE_WIDTH; ++j) {
			glm::vec2 f = glm::mix(rect.ll(),rect.ur(),uv);

			double g = test_elev_fn2(f, code.face, code.zoom);
			data[idx++] = static_cast<float>(g);
			uv.x += d;
		}
		uv.x = 0;
		uv.y += d;
	}

	return;
}


