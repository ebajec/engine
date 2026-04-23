#include <ev2/globe/test_source.h>
#include <ev2/utils/functions.h>

static float test_elev_fn(glm::dvec2 uv, uint8_t f);

static int test_loader_fn(void *usr, uint64_t id, 
					struct ds_buf *buf, struct ds_token *token);
static_assert(std::is_same<decltype(&test_loader_fn), ds_load_fn>::value);

static uint64_t test_loader_find(void *usr, uint64_t id);
static float sample(void *usr, double u, double v, uint8_t f);
static float min_val(void *usr);
static float max_val(void *usr);

static void destroy(struct ds_context *ctx);

int test_data_source_init(struct ds_context **p_ctx)
{
	struct ds_context *ctx = new ds_context;
	*ctx = ds_context{
		.usr = nullptr,
		.vtbl = {
			.destroy = destroy,

			.loader = test_loader_fn,
			.find = test_loader_find,

			.sample = sample,
			.max = max_val,
			.min = min_val,
		}
	};

	*p_ctx = ctx;
	return 0;
}

static constexpr double TEST_AMP = 0.05;
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
	return (float)-TEST_AMP;

}
float max_val(void *usr)
{
	return (float)TEST_AMP;
}

float sample(void *usr, double u, double v, uint8_t f)
{
	return test_elev_fn(glm::dvec2(u,v), f);
}

float test_elev_fn(glm::dvec2 uv, uint8_t f)
{
	double x = 1.0 - 2.0*uv.x;
	double y = 1.0 - 2.0*uv.y;

	double g = weierstrass(x,y, TWOPIf*(float)f);

	g *= TEST_AMP;
	g = smooth_max_zero(g*filter_band(x)*filter_band(y));

	return (float)g;
}

int test_loader_fn(
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

			data[idx++] = test_elev_fn(f, code.face);
			//uv.x += d;
		}
		uv.x = 0;
		//uv.y += d;
	}

	return 0;
}
