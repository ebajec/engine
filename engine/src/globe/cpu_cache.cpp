#include "utils/thread_pool.h"
#include "utils/log.h"

#include "globe/tiling.h"
#include "globe/cpu_cache.h"

#include <imgui.h>

#include <unordered_set>
#include <atomic>
#include <thread>

static int MAX_TILES_IN_FLIGHT = std::thread::hardware_concurrency()/2;
static std::atomic_int g_tiles_in_flight = 0;

static int create_cpu_tile_page(void *usr, alc_page_handle_t *p_handle)
{
	const TileCacheSegment *cache = static_cast<TileCacheSegment*>(usr);

	uint8_t * mem = new uint8_t[cache->tile_size*cache->alc->page_size];
	uintptr_t ptr = reinterpret_cast<uintptr_t>(mem);

	*p_handle = static_cast<uint64_t>(ptr);

	return 0;
}

static int destroy_cpu_tile_page(void *usr, alc_page_handle_t handle)
{
	//const TileCPUCache *cache = static_cast<TileCPUCache*>(usr);
	uint8_t *mem = reinterpret_cast<uint8_t*>(handle); 
	delete[] mem;

	return 0;
}

TileCacheSegment 
*TileCacheSegment::create(size_t tile_size, size_t capacity)
{
	TileCacheSegment *cache = new TileCacheSegment{};
	cache->tile_size = tile_size;
	cache->tile_cap = (std::max(capacity,(size_t)1) - 1)/tile_size + 1;

	alc_table *ct = nullptr;
	alc_create_info ci = {
		.capacity = cache->tile_cap,
		.page_size = TILE_CPU_PAGE_SIZE,
		.usr = cache,
		.page_create = &create_cpu_tile_page,
		.page_destroy = &destroy_cpu_tile_page
	};

	if (alc_create(&ct, &ci) < 0)
		goto cleanup;

	cache->alc.reset(ct);

	return cache;
cleanup:
	delete cache;
	return nullptr;
}

TileCacheSegment::~TileCacheSegment()
{

}

static alc_entry * alc_entry_get(alc_table *ct, alc_index idx)
{
	return &ct->pages[idx.page].entries[idx.ent];
}

TileCode TileCacheSegment::find_best(TileCode code)
{
	auto end = alc->map.end();
	auto it = alc->map.find(tile_code_pack(code));

	alc_status status = ALC_STATUS_EMPTY;

	while (code.zoom > 0 && status != ALC_STATUS_READY) {
		code.idx >>= 2;
		--code.zoom;

		it = alc->map.find(tile_code_pack(code));

		if (it != end) {
			alc_index idx = *it->second;
			alc_entry *ent = alc_entry_get(alc.get(), idx);
			status = alc_state_status(ent->state.load());
		}
	} 

	if (status == ALC_STATUS_READY) {
		alc_lru_list::iterator l_it = it->second;
		alc->lru.splice(alc->lru.begin(), alc->lru, l_it);
		return tile_code_unpack(it->first);
	}

	//log_info("No loaded parent found for tile %d",in);

	return TILE_CODE_NONE;
}

uint8_t *TileCacheSegment::get_block(alc_index idx) const
{
	alc_page_handle_t pg_handle = alc->pages[idx.page].handle; 

	uint8_t *mem = reinterpret_cast<uint8_t*>(pg_handle);

	return &mem[idx.ent*tile_size];
}

const uint8_t *TileCacheSegment::acquire_block(TileCode code, size_t *size,
									   std::atomic_uint64_t **p_state) const
{
	auto it = alc->map.find(tile_code_pack(code));

	if (it == alc->map.end()) {
		log_error("TileDataCache::get_block : Failed to find tile with id %ld",
			tile_code_pack(code));
		return nullptr;
	}

	alc_index idx = *(it->second);
	alc_entry *ent = alc_entry_get(alc.get(),idx);

	if (p_state) *p_state = &ent->state;
	if (size) *size = tile_size;

	return get_block(idx);
}

std::optional<tc_ref> TileCacheSegment::acquire(TileCode code) const
{
	uint64_t id = tile_code_pack(code);

	auto it = alc->map.find(id);
	if (it == alc->map.end()) {
		log_error("acquire_block: Failed to find tile with code %ld (face=%d,zoom=%d,idx=%d)",
			id, code.face,code.zoom,code.idx);
		return std::nullopt;
	}

	alc_index idx = *it->second;
	alc_entry *ent = alc_entry_get(alc.get(),idx);

	uint64_t state = ent->state.load(std::memory_order_relaxed);
	alc_state desired;
	do {
		if (alc_state_status(state) != ALC_STATUS_READY)
			return std::nullopt;

		desired = alc_state_unpack(state); 
		desired.status = ALC_STATUS_READY;
		++desired.refs;
	} while (!ent->state.compare_exchange_weak(state, alc_state_pack(desired),
											std::memory_order_acq_rel, std::memory_order_relaxed));

	//log_info("Acquired tile %d from CPU cache");

	tc_ref ref = {
		.data = get_block(idx),
		.size = tile_size,
		.p_state = &ent->state
	};

	return ref;
}
void TileCacheSegment::release_block(tc_ref ref) const
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

	//log_info("Released tile %d from CPU cache");
}

int my_cancel(struct ds_token *tok)
{
	alc_atomic_state *p_state = static_cast<alc_atomic_state*>(tok->usr);
	alc_atomic_state state = p_state->load(std::memory_order_relaxed);
	return alc_state_status(state) == ALC_STATUS_CANCELLED;
}

static void load_thread_fn(
	const TileDataSource *source, 
	TileCode code,
	alc_atomic_state *p_state, 
	uint8_t* dst)
{
	uint64_t state_pkd = p_state->load(std::memory_order_relaxed);
	alc_state desired;
	do {
		alc_state state = alc_state_unpack(state_pkd);
		desired = state;
		// If it was cancelled then we are the one stuff is waiting on to 
		// se the status back to empty
		if (state.status == ALC_STATUS_CANCELLED) {
			desired.status = ALC_STATUS_EMPTY;
			p_state->store(alc_state_pack(desired), std::memory_order_relaxed);
			log_info("load cancelled successfully (tile %d)",code);

			return;
		} else {
			desired.status = ALC_STATUS_LOADING;
		}
	} while (!p_state->compare_exchange_weak(state_pkd, alc_state_pack(desired), 
										std::memory_order_acq_rel,std::memory_order_relaxed));

	static const struct ds_token_vtbl vtbl = {
		.is_cancelled = &my_cancel
	};

	struct ds_token tok = {
		.usr = p_state,
		.vtbl = &vtbl
	};

	struct ds_buf buf = {
		.dst = dst,
		.cap = TILE_SIZE
	};

	uint64_t code_u64 = tile_code_pack(code);

	source->m_loader(source->m_usr, code_u64, &buf, &tok);

	state_pkd = p_state->load(std::memory_order_relaxed);
	do {
		alc_state state = alc_state_unpack(state_pkd);
		desired = state;
		if (state.status == ALC_STATUS_CANCELLED)
			desired.status = ALC_STATUS_EMPTY;
		else 
			desired.status = ALC_STATUS_READY;
	} while(!p_state->compare_exchange_weak(state_pkd, alc_state_pack(desired),
										 std::memory_order_acq_rel, std::memory_order_relaxed));

	if (desired.status == ALC_STATUS_EMPTY) 
		log_info("load cancelled successfully (tile %d)",code);

	//log_info("Tile %d ready",ent->code);
}

void TileCacheSegment::update(
	TileDataSource const *source, size_t count,
	TileCode const *tiles, TileCode *out
)
{
	std::vector<TileCode> available (count);

	for (size_t i = 0; i < count; ++i) {
		TileCode code = tiles[i];
		TileCode found = source->find(code);
		available[i] = found;
	}

	std::vector<TileCode> loaded (count,TILE_CODE_NONE);

	std::vector<std::pair<alc_index,alc_entry*>> loads;

	// TODO: Doing this to avoid duplicate loads right now - could be better
	std::unordered_set<uint64_t> unique_loads;

	for (size_t i = 0; i < count; ++i) {
		TileCode ideal = available[i];

		uint64_t ideal_u64 = tile_code_pack(ideal);
		alc_result res = alc_get(alc.get(),ideal_u64);

		if (res.needs_load && unique_loads.insert(ideal_u64).second)
			loads.push_back({res.idx,res.p_ent});

		TileCode code = res.is_ready ? ideal : find_best(ideal); 

		out[i] = code;
	}

	for (auto [idx, ent] : loads) {
		uint8_t *dst = get_block(idx);

		if (g_tiles_in_flight < MAX_TILES_IN_FLIGHT) {
			//log_info("Loading tile %d",ent->code);

			ent->state.store(alc_state_pack({
				.status = ALC_STATUS_QUEUED,
				.flags = 0,
				.gen = 0,
				.refs = 0
			}));
			g_schedule_background([=](){
				++g_tiles_in_flight;
				load_thread_fn(
					source, 
					tile_code_unpack(ent->key), 
					&ent->state, 
					dst
				);
				--g_tiles_in_flight;
			});
		}
	}

	return;
}


