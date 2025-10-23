#include "globe/tile_cache.h"
#include "globe/async_lru_cache.h"
#include "globe/cpu_cache.h"

#include "utils/log.h"

#include <unordered_map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <thread>

#include <cstdint>

static int MAX_TILES_IN_FLIGHT = std::thread::hardware_concurrency()/2;
static std::atomic_int g_tiles_in_flight = 0;
size_t g_tile_cap = 2048;
size_t g_tile_size= 256;

//------------------------------------------------------------------------------
// implementation

struct SourceEntry
{
	std::unique_ptr<TileCacheSegment> cache;
	std::shared_mutex sync;
};

struct TileCache {
	std::unordered_map<uint64_t, std::unique_ptr<SourceEntry>> map;
	std::shared_mutex sync;
} g_cache;


SourceEntry *get_source_entry(TileDataSource const *source)
{
	uint64_t id = source->id();
	std::shared_lock<std::shared_mutex> lock(g_cache.sync);
	auto it = g_cache.map.find(id);

	return it == g_cache.map.end() ? nullptr : it->second.get();
}

tc_error register_source_entry(TileDataSource const *src, SourceEntry **p_ent)
{
	std::unique_ptr<TileCacheSegment> cache(
		TileCacheSegment::create(TILE_SIZE*sizeof(float), (size_t)1*GIGABYTE)
	);

	if (!cache)
		return TC_EREGISTER;

	uint64_t id = src->id();

	std::unique_ptr<SourceEntry> ent = std::make_unique<SourceEntry>();
	ent->cache = std::move(cache);

	*p_ent = ent.get(); 

	std::unique_lock<std::shared_mutex> lock(g_cache.sync);
	g_cache.map[id] = std::move(ent);

	return TC_OK;
}

//------------------------------------------------------------------------------
// interface

tc_error tc_load(TileDataSource const *source, size_t count,
				 TileCode const *tiles, TileCode *out)  
{
	tc_error err = TC_OK;

	SourceEntry *ent = get_source_entry(source);

	if (!ent && (err = register_source_entry(source, &ent))) {
		return err;
	}

	if (count > g_tile_cap) {
		log_warn("Number of requested tiles (%lld) exceeds cache size", count);
		count = g_tile_cap;
	}

	ent->cache->update(source, count, tiles, out);

	return TC_OK;
}
tc_error tc_acquire(TileDataSource const *source, TileCode id,
					tc_ref *p_ref)
{
	SourceEntry *ent = get_source_entry(source);

	if (!ent)
		return TC_ENULL;

	std::optional<tc_ref> ref = ent->cache->acquire(id);

	if (ref) {
		*p_ref = *ref;
		return TC_OK;
	}

	return TC_ENULL;
}
void tc_release(tc_ref ref)
{
	uint64_t state = ref.p_state->load();
	uint64_t desired;
	do {
		desired = alc_state_pack({
			.status = ALC_STATUS_READY, 
			.flags = 0,
			.gen = 0,
			.refs = alc_state_refs(state) - 1
		});
	} while (!ref.p_state->compare_exchange_weak(state, desired));
}

