#include "engine/globe/test_source.h"

#include "terrain.h"

#include <algorithm>
#include <atomic>

CPUTileCache 
*CPUTileCache::create()
{
	static std::atomic_uint64_t id_ctr = 0;

	CPUTileCache *source = new CPUTileCache;

	if (test_data_source_init(&source->ds)) {
		return nullptr;
	}
	source->m_id = ++id_ctr;

	tc_create(&source->tc, TILE_SIZE*sizeof(float), (size_t)1*GIGABYTE);

	return source;
}

CPUTileCache::~CPUTileCache()
{
	tc_destroy(tc);
	ds_context_destroy(ds);
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

float CPUTileCache::tile_min(TileCode code) const
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
float CPUTileCache::tile_max(TileCode code) const
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

