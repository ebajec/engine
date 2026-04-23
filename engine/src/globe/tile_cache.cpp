#include <ev2/utils/log.h>
#include <ev2/globe/tiling.h>

#include "utils/thread_pool.h"
#include "tile_cache.h"

#include <imgui.h>

#include <unordered_set>
#include <atomic>
#include <thread>

struct tc_cache
{
	alc_table *alc;
	size_t tile_size;
	size_t tile_cap;
};

enum {
	LOAD_SUCCESS,
	LOAD_FAILED
};


static int MAX_TILES_IN_FLIGHT = std::thread::hardware_concurrency()/2;
static std::atomic_int g_tiles_in_flight = 0;


static int create_cpu_tile_page(void *usr, alc_page_handle_t *p_handle)
{
	const tc_cache *tc = static_cast<tc_cache*>(usr);

	uint8_t * mem = new uint8_t[tc->tile_size*tc->alc->page_size];
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

static alc_entry * alc_entry_get(alc_table *ct, alc_index idx)
{
	return &ct->pages[idx.page].entries[idx.ent];
}

static TileCode find_best(const tc_cache *tc, TileCode code)
{
	auto end = tc->alc->map.end();
	auto it = tc->alc->map.find(tile_code_pack(code));

	alc_status status = ALC_STATUS_EMPTY;

	while (code.zoom > 0 && status != ALC_STATUS_READY) {
		code.idx >>= 2;
		--code.zoom;

		it = tc->alc->map.find(tile_code_pack(code));

		if (it != end) {
			alc_index idx = *it->second;
			alc_entry *ent = alc_entry_get(tc->alc, idx);
			status = alc_state_status(ent->state.load());
		}
	} 

	if (status == ALC_STATUS_READY) {
		alc_lru_list::iterator l_it = it->second;
		tc->alc->lru.splice(tc->alc->lru.begin(), tc->alc->lru, l_it);
		return tile_code_unpack(it->first);
	}

	//log_info("No loaded parent found for tile %d",in);

	return TILE_CODE_NONE;
}

static uint8_t *get_block(const tc_cache *tc, alc_index idx)
{
	alc_page_handle_t pg_handle = tc->alc->pages[idx.page].handle; 

	uint8_t *mem = reinterpret_cast<uint8_t*>(pg_handle);

	return &mem[idx.ent*tc->tile_size];
}

static int my_cancel(struct ds_token *tok)
{
	alc_atomic_state *p_state = static_cast<alc_atomic_state*>(tok->usr);
	alc_atomic_state state = p_state->load(std::memory_order_relaxed);
	bool cancelled = alc_state_status(state) == ALC_STATUS_CANCELLED; 
	return cancelled;
}

static int load_thread_fn(
	ds_context const *ds, 
	uint64_t id,
	alc_atomic_state *p_state, 
	ds_buf *buf
)
{
	if (!alc_state_set_loading(p_state)) {
		log_info("load cancelled successfully (tile %d)",id);
		return LOAD_FAILED;
	}

	static const struct ds_token_vtbl vtbl = {
		.is_cancelled = &my_cancel
	};

	struct ds_token tok = {
		.usr = p_state,
		.vtbl = &vtbl
	};

	ds->vtbl.loader(ds->usr, id, buf, &tok);

	if (!alc_state_set_ready(p_state)) {
		log_info("load cancelled successfully (tile %d)",id);
		return LOAD_FAILED;
	}

	return LOAD_SUCCESS;
}

tc_error tc_create(tc_cache **p_tc, size_t tile_size, size_t capacity)
{
	tc_cache *tc = new tc_cache{};
	tc->tile_size = tile_size;
	tc->tile_cap = (std::max(capacity,(size_t)1) - 1)/tile_size + 1;

	alc_params p = {
		.capacity = tc->tile_cap,
		.page_size = TILE_CPU_PAGE_SIZE,
		.usr = tc,
		.page_create = &create_cpu_tile_page,
		.page_destroy = &destroy_cpu_tile_page
	};

	if (alc_create(&tc->alc, &p) < 0)
		goto tc_create_failed;

	*p_tc = tc;

	return TC_OK;

tc_create_failed:
	delete tc;
	return TC_ENULL;
}

void tc_destroy(tc_cache *tc)
{
	if (!tc) 
		return;

	alc_destroy(tc->alc);
	delete tc;
}

tc_error tc_load(
	tc_cache *tc,
	ds_context const *ds,

	void *usr, 
	tc_post_load_fn post_load,

	size_t count, 
	tile_code_t const *tiles,
	tile_code_t *out
)
{
	std::vector<TileCode> available (count);

	for (size_t i = 0; i < count; ++i) {
		uint64_t code = tiles[i];
		TileCode found = tile_code_unpack(ds->vtbl.find(ds->usr, code));
		available[i] = found;
	}

	std::vector<TileCode> loaded (count,TILE_CODE_NONE);

	struct load_token_t
	{
		alc_index idx;
		alc_entry *ent;
	};

	std::vector<load_token_t> loads;

	// TODO: Doing this to avoid duplicate loads right now - could be better
	std::unordered_set<uint64_t> unique_loads;

	for (size_t i = 0; i < count; ++i) {
		TileCode ideal = available[i];

		uint64_t ideal_u64 = tile_code_pack(ideal);
		alc_result res = alc_get(tc->alc,ideal_u64);

		if (res.needs_load && unique_loads.insert(ideal_u64).second)
			loads.push_back(load_token_t{
				.idx = res.idx,
				.ent = res.p_ent
			});

		TileCode code = res.is_ready ? ideal : find_best(tc, ideal); 

		out[i] = tile_code_pack(code);
	}

	const bool has_post_load = post_load;

	for (size_t i = 0; i < loads.size(); ++i) {
		load_token_t tok = loads[i];
		uint8_t *dst = get_block(tc, tok.idx);

		if (g_tiles_in_flight < MAX_TILES_IN_FLIGHT) {
			//log_info("Loading tile %d",ent->code);

			tok.ent->state.store(alc_state_pack({
				.status = ALC_STATUS_QUEUED,
				.flags = 0,
				.gen = 0,
				.refs = 0
			}));

			g_schedule_background([=](){
				++g_tiles_in_flight;

				struct ds_buf buf = {
					.dst = dst,
					.size = TILE_SIZE*sizeof(float)
				};

				uint64_t id = tok.ent->key; 

				int status = load_thread_fn(
					ds, 
					id, 
					&(tok.ent->state), 
					&buf
				);
				--g_tiles_in_flight;

				if (has_post_load && status == LOAD_SUCCESS) 
					post_load(usr, id, &buf);
			});
		}
	}

	return TC_OK;
}

tc_error tc_acquire(const tc_cache *tc, tile_code_t id, tc_ref *p_ref)
{
	TileCode code = tile_code_unpack(id);

	auto it = tc->alc->map.find(id);
	if (it == tc->alc->map.end()) {
		log_error("acquire_block: Failed to find tile with code %ld (face=%d,zoom=%d,idx=%d)",
			id, code.face,code.zoom,code.idx);
		return TC_ENULL;
	}

	alc_index idx = *it->second;
	alc_entry *ent = alc_entry_get(tc->alc,idx);

	if (!alc_state_inc_ref(&ent->state))
		return TC_ENULL;

	//log_info("Acquired tile %d from CPU cache");

	tc_ref ref = {
		.data = get_block(tc, idx),
		.size = tc->tile_size,
		.p_state = &ent->state
	};

	*p_ref = ref;

	return TC_OK;
}

void tc_release(tc_ref ref)
{
	alc_state_dec_ref(ref.p_state);

	//log_info("Released tile %d from CPU cache");
}

