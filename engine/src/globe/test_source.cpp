#include "engine/globe/test_source.h"

static float test_elev_fn(glm::dvec2 uv, uint8_t f);

static int test_loader_fn2(void *usr, uint64_t id, 
					struct ds_buf *buf, struct ds_token *token);
static_assert(std::is_same<decltype(&test_loader_fn2), ds_load_fn>::value);

static uint64_t test_loader_find(void *usr, uint64_t id);
static float sample(void *usr, float u, float v, uint8_t f);
static float min_val(void *usr);
static float max_val(void *usr);

static void destroy(struct ds_context *ctx);

int test_data_source_init(struct ds_context **p_ctx)
{
	struct ds_context *ctx = new ds_context;
	*ctx = {
		.usr = nullptr,
		.vtbl = {
			.destroy = destroy,

			.loader = test_loader_fn2,
			.find = test_loader_find,

			.sample = sample,
			.max = max_val,
			.min = min_val,
		}
	};

	*p_ctx = ctx;
	return 0;
}

static constexpr double TEST_AMP = 0.001;
static constexpr double TEST_FREQ = 1200;
//------------------------------------------------------------------------------
// Test loader functions
void destroy(struct ds_context *ctx)
{
	delete ctx;
}

uint64_t test_loader_find(void *usr, uint64_t id)
{
	TileCode code = tile_code_unpack(id);
	while (code.zoom > 16) {
		code.idx >>= 2;
		--code.zoom;
	} 

	return tile_code_pack(code);
}

float min_val(void *usr)
{
	return -TEST_AMP;

}
float max_val(void *usr)
{
	return TEST_FREQ;
}

static constexpr size_t coeff_count = 25;
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

static float sample(void *usr, float u, float v, uint8_t f)
{
	return test_elev_fn(glm::dvec2(u,v), f);
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
		double c = TEST_FREQ*(idx);
		glm::dvec3 h = 0.5*TEST_FREQ*phases[i]*TWOPI;
		g += (coeffs[i]/(double)idx)*(sin(c*p.x - h.x)*sin(c*p.y - h.y)*sin(c*p.z - h.z));
	}

	g *= TEST_AMP;

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

		uv.y = (float)i*d;

		for (size_t j = 0; j < TILE_WIDTH; ++j) {
			uv.x = (float)j*d;
			glm::vec2 f = glm::mix(rect.ll(),rect.ur(),uv);

			double g = test_elev_fn2(f, code.face, code.zoom);
			data[idx++] = static_cast<float>(g);
			//uv.x += d;
		}
		uv.x = 0;
		//uv.y += d;
	}

	return 0;
}
