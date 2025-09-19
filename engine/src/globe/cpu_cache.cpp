#include "utils/thread_pool.h"
#include "utils/log.h"

#include "globe/tiling.h"
#include "globe/cpu_cache.h"

#include <imgui.h>

#include <atomic>
#include <thread>

static int MAX_TILES_IN_FLIGHT = std::thread::hardware_concurrency()/2;
static std::atomic_int g_tiles_in_flight = 0;

static int create_cpu_tile_page(void *usr, pct_page_handle_t *p_handle)
{
	const TileCPUCache *cache = static_cast<TileCPUCache*>(usr);

	uint8_t * mem = new uint8_t[cache->m_tile_size*cache->m_ct->page_size];
	uintptr_t ptr = reinterpret_cast<uintptr_t>(mem);

	*p_handle = static_cast<uint64_t>(ptr);

	return 0;
}

TileCPUCache 
*TileCPUCache::create(size_t tile_size, size_t capacity)
{
	TileCPUCache *cache = new TileCPUCache{};
	cache->m_tile_size = tile_size;
	cache->m_tile_cap = (std::max(capacity,(size_t)1) - 1)/tile_size + 1;

	pct_table *ct = nullptr;
	pct_table_create_info ci = {
		.capacity = cache->m_tile_cap,
		.page_size = TILE_CPU_PAGE_SIZE,
		.usr = cache,
		.page_create = &create_cpu_tile_page,
	};

	if (pct_table_create(&ct, &ci) < 0)
		goto cleanup;

	cache->m_ct.reset(ct);

	return cache;
cleanup:
	delete cache;
	return nullptr;
}

static pct_entry * pct_entry_get(pct_table *ct, pct_index idx)
{
	return &ct->pages[idx.page].entries[idx.ent];
}

TileCode TileCPUCache::find_best(TileCode code)
{
	auto end = m_ct->map.end();
	auto it = m_ct->map.find(code.u64);

	pct_status status = PCT_STATUS_EMPTY;

	while (code.zoom > 0 && status != PCT_STATUS_READY) {
		code.idx >>= 2;
		--code.zoom;

		it = m_ct->map.find(code.u64);

		if (it != end) {
			pct_index idx = *it->second;
			pct_entry *ent = pct_entry_get(m_ct.get(), idx);
			status = pct_state_status(ent->state.load());
		}
	} 

	if (status == PCT_STATUS_READY) {
		pct_lru_list::iterator l_it = it->second;
		m_ct->lru.splice(m_ct->lru.begin(), m_ct->lru, l_it);
		return TileCode{.u64 = it->first};
	}

	//log_info("No loaded parent found for tile %d",in);

	return TILE_CODE_NONE;
}

uint8_t *TileCPUCache::get_block(pct_index idx) const
{
	pct_page_handle_t pg_handle = m_ct->pages[idx.page].handle; 

	uint8_t *mem = reinterpret_cast<uint8_t*>(pg_handle);

	return &mem[idx.ent*m_tile_size];
}

const uint8_t *TileCPUCache::acquire_block(TileCode code, size_t *size,
									   std::atomic_uint64_t **p_state) const
{
	auto it = m_ct->map.find(code.u64);

	if (it == m_ct->map.end()) {
		log_error("TileDataCache::get_block : Failed to find tile with id %ld",
			code.u64);
		return nullptr;
	}

	pct_index idx = *(it->second);
	pct_entry *ent = pct_entry_get(m_ct.get(),idx);

	if (p_state) *p_state = &ent->state;
	if (size) *size = m_tile_size;

	return get_block(idx);
}

std::optional<TileDataRef> TileCPUCache::acquire_block(TileCode code) const
{
	auto it = m_ct->map.find(code.u64);

	if (it == m_ct->map.end()) {
		log_error("acquire_block: Failed to find tile with code %ld (face=%d,zoom=%d,idx=%d)",
			code.u64, code.face,code.zoom,code.idx);
		return std::nullopt;
	}

	pct_index idx = *it->second;
	pct_entry *ent = pct_entry_get(m_ct.get(),idx);

	uint64_t state = ent->state.load(std::memory_order_relaxed);
	pct_state desired;
	do {
		if (pct_state_status(state) != PCT_STATUS_READY)
			return std::nullopt;

		desired = pct_state_unpack(state); 
		desired.status = PCT_STATUS_READY;
		++desired.refs;
	} while (!ent->state.compare_exchange_weak(state, pct_state_pack(desired),
											std::memory_order_acq_rel, std::memory_order_relaxed));

	//log_info("Acquired tile %d from CPU cache");

	TileDataRef ref = {
		.data = get_block(idx),
		.size = m_tile_size,
		.p_state = &ent->state
	};

	return ref;
}
void TileCPUCache::release_block(TileDataRef ref) const
{
	uint64_t state = ref.p_state->load();
	uint64_t desired;
	do {
		desired = pct_state_pack({
			.status = PCT_STATUS_READY, 
			.flags = 0,
			.gen = 0,
			.refs = pct_state_refs(state) - 1
		});
	} while (!ref.p_state->compare_exchange_weak(state, desired));

	//log_info("Released tile %d from CPU cache");
}

static void load_thread_fn(
	const TileDataSource *source, 
	TileCode code,
	pct_atomic_state *p_state, 
	uint8_t* dst)
{
	uint64_t state_pkd = p_state->load(std::memory_order_relaxed);
	pct_state desired;
	do {
		pct_state state = pct_state_unpack(state_pkd);
		desired = state;
		// If it was cancelled then we are the one stuff is waiting on to 
		// se the status back to empty
		if (state.status == PCT_STATUS_CANCELLED) {
			desired.status = PCT_STATUS_EMPTY;
			p_state->store(pct_state_pack(desired), std::memory_order_relaxed);
			log_info("load cancelled successfully (tile %d)",code);

			return;
		} else {
			desired.status = PCT_STATUS_LOADING;
		}
	} while (!p_state->compare_exchange_weak(state_pkd, pct_state_pack(desired), 
										std::memory_order_acq_rel,std::memory_order_relaxed));

	source->m_loader(code,dst,source->m_usr,p_state);

	state_pkd = p_state->load(std::memory_order_relaxed);
	do {
		pct_state state = pct_state_unpack(state_pkd);
		desired = state;
		if (state.status == PCT_STATUS_CANCELLED)
			desired.status = PCT_STATUS_EMPTY;
		else 
			desired.status = PCT_STATUS_READY;
	} while(!p_state->compare_exchange_weak(state_pkd, pct_state_pack(desired),
										 std::memory_order_acq_rel, std::memory_order_relaxed));

	if (desired.status == PCT_STATUS_EMPTY) 
		log_info("load cancelled successfully (tile %d)",code);

	//log_info("Tile %d ready",ent->code);
}

std::vector<TileCode> TileCPUCache::update(
	const TileDataSource& source, const std::span<TileCode> tiles)
{
	std::vector<TileCode> available (tiles.size());

	for (size_t i = 0; i < tiles.size(); ++i) {
		TileCode code = tiles[i];
		available[i] = source.find(code);
	}

	std::vector<TileCode> loaded (tiles.size(),TILE_CODE_NONE);

	// TODO : We can have duplicate loads right now - fix this
	std::vector<std::pair<pct_index,pct_entry*>> loads;

	size_t count = std::min(available.size(),m_tile_cap);

	if (count < available.size()) {
		log_warn("Number of requested tiles exceeds cache size");
	}

	for (size_t i = 0; i < count; ++i) {
		TileCode ideal = available[i];
		pct_load_result res = pct_table_load(m_ct.get(),ideal.u64);

		if (res.needs_load)
			loads.push_back({res.idx,res.p_ent});

		TileCode code = res.is_ready ? ideal : find_best(ideal); 

		loaded[i] = code;
	}

	for (auto [idx, ent] : loads) {
		uint8_t *dst = get_block(idx);

		if (g_tiles_in_flight < MAX_TILES_IN_FLIGHT) {
			//log_info("Loading tile %d",ent->code);

			ent->state.store(pct_state_pack({
				.status = PCT_STATUS_QUEUED,
				.flags = 0,
				.gen = 0,
				.refs = 0
			}));
			g_schedule_background([=](){
				++g_tiles_in_flight;
				load_thread_fn(
					&source, 
					TileCode{.u64 = ent->key}, 
					&ent->state, 
					dst
				);
				--g_tiles_in_flight;
			});
		}
	}

	return loaded;
}


