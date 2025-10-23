#include "utils/log.h"

#include "globe/async_lru_cache.h"

#include <thread>

#include <cassert>
#include <cstdlib>

static int alc_create_page(alc_table *alc, alc_page *page)
{
	size_t size = alc->page_size;
	page->free_list.resize(size);

	for (size_t i = 0; i < size; ++i) {
		page->free_list[i] = static_cast<uint32_t>(size - i - 1);
	}

	page->entries = (alc_entry*)calloc(size, sizeof(alc_entry));

	return alc->page_create(alc->usr, &page->handle);
}

static alc_index alc_allocate(alc_table *alc)
{
	if (alc->open_pages.empty()) {
		alc->pages.emplace_back();

		if (alc_create_page(alc, &alc->pages.back()) < 0)
			return ALC_INDEX_NONE;

		alc->open_pages.push(static_cast<uint16_t>(alc->pages.size() - 1));
	}
	
	uint16_t page = alc->open_pages.top(); 

	assert(page < alc->pages.size());

	alc_page *p_page = &alc->pages[page];

	uint32_t ent = p_page->free_list.back();

	assert(ent < alc->page_size);

	p_page->free_list.pop_back();

	if (p_page->free_list.empty()) {
		alc->open_pages.pop();
	}

	return alc_index{
		.page = page,
		.ent = ent,
	};
}

static alc_index alc_evict_one(alc_table *alc)
{
	alc_index idx = alc->lru.back(); 
	alc_entry *ent = &alc->pages[idx.page].entries[idx.ent];

	uint64_t state_packed = ent->state.load(std::memory_order_relaxed);
	alc_state desired = {
		.status = ALC_STATUS_EMPTY, 
		.flags = 0,
		.gen = 0,
		.refs = 0
	};

	do {
		alc_state state = alc_state_unpack(state_packed);
		if (state.refs > 0) {
			log_info("Could not evialc entry %d from cache; in use",ent->key);
			return ALC_INDEX_NONE;
		}

		if (state.status == ALC_STATUS_CANCELLED) {
			return ALC_INDEX_NONE;
		}

		if (state.status == ALC_STATUS_LOADING || 
			state.status == ALC_STATUS_QUEUED)
			desired = {
				.status = ALC_STATUS_CANCELLED, 
				.flags = 0,
				.gen = 0,
				.refs = state.refs
			};
		else 
			desired = {
				.status = ALC_STATUS_EMPTY,
				.flags = 0,
				.gen = 0,
				.refs = 0
			};
	} while (!ent->state.compare_exchange_weak(state_packed, alc_state_pack(desired),
											std::memory_order_acq_rel, std::memory_order_relaxed));
	
	uint64_t key = ent->key;
		
	if (desired.status == ALC_STATUS_CANCELLED) {
		log_info("Cancelled load for entry %d",ent->key);
		return ALC_INDEX_NONE;
	}

	if (alc->map.find(key) == alc->map.end()) {
		log_error("Failed to evict entry at %d; not contained in table!",ent->key);
		return ALC_INDEX_NONE;
	}

	//log_info("Evialced tile %d from CPU cache",ent->code);

	alc->map.erase(key);	
	alc->lru.pop_back();

	return idx;
}

alc_result alc_get(alc_table *alc, uint64_t key)
{
	// note that 'needs_load' and 'is_ready' are false by default
	alc_result res {};

	auto it = alc->map.find(key);
	if (it != alc->map.end()) {

		// Move to front if key is found
		alc_lru_list::iterator l_ent = it->second;
		alc->lru.splice(alc->lru.begin(), alc->lru, l_ent);

		alc_index idx = *it->second;
		alc_entry *ent = &alc->pages[idx.page].entries[idx.ent];
		alc_state state = alc_state_unpack(ent->state.load()); 

		res.p_ent = ent;

		if (state.status == ALC_STATUS_EMPTY) {
			if (state.refs > 0) 
				log_error("Empty entry has %d references",state.refs);

			res.idx = idx;
			res.needs_load = true;

		} else if (state.status == ALC_STATUS_READY) {
			res.is_ready = true;
		}
	} else {
		alc_index idx = alc->map.size() >= alc->capacity ?
	  		alc_evict_one(alc) : alc_allocate(alc);

		if (!idx.is_valid()) 
			return res;

		alc->lru.push_front(idx);
		alc->map[key] = alc->lru.begin();

		alc_entry *ent = &alc->pages[idx.page].entries[idx.ent];
		ent->key = key,
		ent->state = alc_state_pack({
			.status = ALC_STATUS_EMPTY, 
			.flags = 0,
			.gen = 0,
			.refs = 0
		}),

		res.idx = idx;
		res.p_ent = ent;
		res.needs_load = true;
	}

	return res;
}

static alc_entry * alc_entry_get(alc_table *ct, alc_index idx)
{
	return &ct->pages[idx.page].entries[idx.ent];
}

int alc_create(alc_table **p_alc, alc_create_info const *ci)
{
	alc_table *alc = new alc_table {};

	alc->page_create = ci->page_create;
	alc->page_destroy = ci->page_destroy;
	alc->page_size = ci->page_size;
	alc->capacity = ci->capacity;
	alc->usr = ci->usr;

	*p_alc = alc;

	return 0;
}

void alc_destroy(alc_table *alc)
{
	for (auto [key, val] : alc->map) {
		alc_index idx = *val;
		alc_entry *ent = alc_entry_get(alc, idx);

		alc_state state;
		do {
			state = alc_state_unpack(
				ent->state.load(std::memory_order_relaxed)
			);

			std::this_thread::yield();
		} while (state.refs > 0);
	}

	for (alc_page &page : alc->pages) {
		alc->page_destroy(alc->usr,page.handle);
		free(page.entries);
	}
	delete alc;
}

alc_entry *alc_acquire(alc_table *alc, uint64_t key)
{
	auto it = alc->map.find(key);
	if (it == alc->map.end()) {
		return nullptr;
	}

	alc_index idx = *it->second;
	alc_entry *ent = alc_entry_get(alc,idx);

	uint64_t state = ent->state.load(std::memory_order_relaxed);
	alc_state desired;
	do {
		if (alc_state_status(state) != ALC_STATUS_READY)
			return nullptr;

		desired = alc_state_unpack(state); 
		desired.status = ALC_STATUS_READY;
		++desired.refs;
	} while (!ent->state.compare_exchange_weak(state, alc_state_pack(desired),
											std::memory_order_acq_rel, std::memory_order_relaxed));
	return ent;
}
void alc_release(alc_entry *ent)
{
	uint64_t state = ent->state.load(std::memory_order_relaxed);
	uint64_t desired;
	do {
		uint32_t refs = alc_state_refs(state); 

		if (!refs) {
			log_error("alc_release : refcount is zero");
			refs = 1;
		}

		desired = alc_state_pack({
			.status = ALC_STATUS_READY, 
			.flags = 0,
			.gen = 0,
			.refs = refs - 1
		});
	} while (!ent->state.compare_exchange_weak(state, desired, std::memory_order_relaxed));

}

//------------------------------------------------------------------------------
// Compile time tests

static constexpr alc_state alc_state_test_val = {7,13,17,19};

static_assert(alc_state_unpack(alc_state_pack(alc_state_test_val)).status == alc_state_test_val.status);
static_assert(alc_state_unpack(alc_state_pack(alc_state_test_val)).flags == alc_state_test_val.flags);
static_assert(alc_state_unpack(alc_state_pack(alc_state_test_val)).gen == alc_state_test_val.gen);
static_assert(alc_state_unpack(alc_state_pack(alc_state_test_val)).refs == alc_state_test_val.refs);
static_assert(alc_state_pack(alc_state_unpack(0xDEADBEEF)) == 0xDEADBEEF);

