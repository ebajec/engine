#include "engine/globe/test_source.h"
#include "utils/log.h"

#include "terrain.h"

#include <algorithm>
#include <atomic>

CPUTileCache 
*CPUTileCache::create()
{
	static std::atomic_uint64_t id_ctr = 0;

	CPUTileCache *source = new CPUTileCache{};

	if (test_data_source_init(&source->ds))
		goto create_failed;

	if (tc_create(&source->tc, TILE_SIZE*sizeof(float), (size_t)1*GIGABYTE))
		goto create_failed;

	if (mmt_create(&source->mmt, mmt_value_t{.min = -0.1, .max = 0.1}))
		goto create_failed;

	return source;

create_failed:
	if (source->ds)
		ds_context_destroy(source->ds);
	if (source->tc)
		tc_destroy(source->tc);

	delete source;
	return nullptr;
}

CPUTileCache::~CPUTileCache()
{
	tc_destroy(tc);
	ds_context_destroy(ds);
	mmt_destroy(mmt);
}

void post_load(void* usr, uint64_t code, const ds_buf *buf)
{
	CPUTileCache *cache = static_cast<CPUTileCache*>(usr);

	size_t count = buf->size/sizeof(float);

	float *data = static_cast<float*>(buf->dst);
	float min = FLT_MAX, max = -FLT_MAX;

	for (size_t i = 0; i < count; ++i) {
		float f = data[i];
		min = std::min(min, f);
		max = std::max(max, f);
	}

	std::unique_lock<std::mutex> lock(cache->sync);
	cache->updates.push_back(mmt_update{
		.min = min, 
		.max = max,
		.id = code
	});
}

void CPUTileCache::load_tiles(size_t count, const TileCode *tiles, TileCode *out)
{

	tc_error err = tc_load(
		tc, 
		ds,
		this, 
		post_load,
		count, 
		tiles,
		out
	);

	if (err != TC_OK) {
		// Do something...
		//
		// This should only cause missing data to appear though
	}

	{ std::unique_lock<std::mutex> lock(sync);
		working.swap(updates); 
	}

	int inserted = 0;

	for (mmt_update u : working) {
		inserted += mmt_insert_monotonic(mmt, u.id, u.min, u.max);
	}

	if (inserted)
		log_info("Updated min/max tree with %lld values",working.size());

	working.clear();
}

float CPUTileCache::sample_elevation_at(glm::dvec2 uv, uint8_t f) const
{
	return ds->vtbl.sample(ds->usr, uv.x, uv.y, f);
}

float CPUTileCache::sample_elevation_at(glm::dvec3 p) const
{
	glm::dvec2 uv;
	uint8_t f;
	globe_to_cube(p, &uv, &f);

	return ds->vtbl.sample(ds->usr, uv.x, uv.y, f);
}

std::pair<float,float> CPUTileCache::tile_minmax(TileCode code) const
{
	uint64_t u64 = tile_code_pack(code);
	mmt_result_t val = mmt_minmax(mmt, u64);
	return {val.min,val.max};
}

float CPUTileCache::max() const
{
	if (!ds->vtbl.max)
		return 1;
	return ds->vtbl.max(ds->usr);
}
float CPUTileCache::min() const
{
	if (!ds->vtbl.min)
		return -1;
	return -ds->vtbl.min(ds->usr);
}

