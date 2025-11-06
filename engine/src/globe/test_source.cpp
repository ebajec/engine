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

static constexpr double TEST_AMP = 0.1;
static constexpr double TEST_FREQ = 160;
//------------------------------------------------------------------------------
// Test loader functions
void destroy(struct ds_context *ctx)
{
	delete ctx;
}

uint64_t test_loader_find(void *usr, uint64_t id)
{
	TileCode code = tile_code_unpack(id);
	while (code.zoom > 10) {
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
	return TEST_AMP;
}

static constexpr size_t coeff_count = 100;
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

/// @brief smooth function that is zero up to the first derivative at -1 and 1
static inline double filter_band(double x)
{
	double a = 1 - x*x*x*x;
	return 2*a*a/(1 + a*a);
}

static inline double W(double x)
{
	static constexpr double b = 6;

	return -(1.0/(b*b))*log(1/(1.0 + exp((b*b)*x)));
}

static double test_elev_fn1(glm::dvec2 uv, uint8_t f, uint8_t zoom)
{
	static constexpr double 
	L = 0.5, 
	D = 2.2,
	G = 1.1, 
	gamma =	2.5;
	static constexpr size_t M = 12, N = 8;

	double A = L*pow(G/D,D-2.0)*sqrt(log(gamma)/(double)M); 

	static int init = 0;

	static double phi[M][N];
	static double cos_phi[M][N];

	static double gammaD3n[N];
	static double gamman[N];

	if (!init++) {
		for (size_t m = 0; m < M; ++m) {
			for (size_t n = 0; n < N; ++n) {
				phi[m][n] = TWOPI*urandf1();
				cos_phi[m][n] = cos(phi[m][n]);
			}
		}

		for (size_t n = 0; n < N; ++n) {
			gammaD3n[n] = pow(gamma, (D - 3.0)*n);
			gamman[n] = pow(gamma, n);
		}
	}

	double g = 0;

	double x = 1.0 - 2.0*uv.x;
	double y = 1.0 - 2.0*uv.y;

	double r = hypot(x,y);
	double tht = atan2(y,x);

	for (size_t m = 0; m < M; ++m) {
		for (size_t n = 0; n < N; ++n) {
			double phi_mn = (double)f + phi[m][n];

			g += gammaD3n[n] * (cos_phi[m][n] - cos(TWOPI*gamman[n]*r*cos(tht - PI*m/M)/L + phi_mn));
		}
	}

	g *= A*TEST_AMP;

	return W(g*filter_band(x)*filter_band(y));

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
		glm::dvec3 h = phases[i]*TWOPI;
		g += (coeffs[i]/(double)(2*idx))*(sin(c*p.x - h.x)*sin(c*p.y - h.y)*sin(c*p.z - h.z));
	}

	g *= TEST_AMP;

	return g;
}

float test_elev_fn(glm::dvec2 uv, uint8_t f)
{
	return (float)test_elev_fn1(uv, f,coeff_count);
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

			double g = test_elev_fn1(f, code.face, code.zoom);
			data[idx++] = static_cast<float>(g);
			//uv.x += d;
		}
		uv.x = 0;
		//uv.y += d;
	}

	return 0;
}
