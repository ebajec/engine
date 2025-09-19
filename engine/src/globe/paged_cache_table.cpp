#include "utils/log.h"

#include "globe/paged_cache_table.h"

#include <cassert>
#include <cstdlib>

static int pct_create_page(pct_table *pct, pct_page *page)
{
	size_t size = pct->page_size;
	page->free_list.resize(size);

	for (size_t i = 0; i < size; ++i) {
		page->free_list[i] = static_cast<uint32_t>(size - i - 1);
	}

	page->entries = (pct_entry*)calloc(size, sizeof(pct_entry));

	return pct->page_create(pct->usr, &page->handle);
}

static pct_index pct_allocate(pct_table *pct)
{
	if (pct->open_pages.empty()) {
		pct->pages.emplace_back();

		if (pct_create_page(pct, &pct->pages.back()) < 0)
			return PCT_INDEX_NONE;

		pct->open_pages.push(static_cast<uint16_t>(pct->pages.size() - 1));
	}
	
	uint16_t page = pct->open_pages.top(); 

	assert(page < pct->pages.size());

	pct_page *p_page = &pct->pages[page];

	uint32_t ent = p_page->free_list.back();

	assert(ent < pct->page_size);

	p_page->free_list.pop_back();

	if (p_page->free_list.empty()) {
		pct->open_pages.pop();
	}

	return pct_index{
		.page = page,
		.ent = ent,
	};
}

static pct_index pct_evict_one(pct_table *pct)
{
	pct_index idx = pct->lru.back(); 
	pct_entry *ent = &pct->pages[idx.page].entries[idx.ent];

	uint64_t state_packed = ent->state.load(std::memory_order_relaxed);
	pct_state desired = {
		.status = PCT_STATUS_EMPTY, 
		.flags = 0,
		.gen = 0,
		.refs = 0
	};

	do {
		pct_state state = pct_state_unpack(state_packed);
		if (state.refs > 0) {
			log_info("Could not evipct entry %d from cache; in use",ent->key);
			return PCT_INDEX_NONE;
		}

		if (state.status == PCT_STATUS_CANCELLED) {
			return PCT_INDEX_NONE;
		}

		if (state.status == PCT_STATUS_LOADING || 
			state.status == PCT_STATUS_QUEUED)
			desired = {
				.status = PCT_STATUS_CANCELLED, 
				.flags = 0,
				.gen = 0,
				.refs = state.refs
			};
		else 
			desired = {
				.status = PCT_STATUS_EMPTY,
				.flags = 0,
				.gen = 0,
				.refs = 0
			};
	} while (!ent->state.compare_exchange_weak(state_packed, pct_state_pack(desired),
											std::memory_order_acq_rel, std::memory_order_relaxed));

	if (desired.status == PCT_STATUS_CANCELLED) {
		log_info("Cancelled load for tile %d",ent->key);
		return PCT_INDEX_NONE;
	}

	if (pct->map.find(ent->key) == pct->map.end()) {
		log_error("Failed to evict entry at %d; not contained in table!",ent->key);
		return PCT_INDEX_NONE;
	}

	//log_info("Evipcted tile %d from CPU cache",ent->code);

	pct->map.erase(ent->key);	
	pct->lru.pop_back();

	return idx;
}

pct_load_result pct_table_load(pct_table *pct, uint64_t key)
{
	pct_load_result res {};

	auto it = pct->map.find(key);
	if (it != pct->map.end()) {
		pct_lru_list::iterator l_ent = it->second;
		pct->lru.splice(pct->lru.begin(), pct->lru, l_ent);

		pct_index idx = *it->second;
		pct_entry *ent = &pct->pages[idx.page].entries[idx.ent];
		pct_state state = pct_state_unpack(ent->state.load()); 

		if (state.status == PCT_STATUS_EMPTY) {
			if (state.refs > 0) 
				log_error("Empty tile has %d references",state.refs);

			res.idx = idx;
			res.p_ent = ent;
			res.needs_load = true;

		} else if (state.status == PCT_STATUS_READY) {
			res.is_ready = true;
		}
	} else {
		pct_index idx = pct->map.size() >= pct->capacity ?
	  		pct_evict_one(pct) : pct_allocate(pct);

		if (!idx.is_valid()) 
			return res;

		pct->lru.push_front(idx);
		pct->map[key] = pct->lru.begin();

		pct_entry *ent = &pct->pages[idx.page].entries[idx.ent];
		ent->key = key,
		ent->state = pct_state_pack({
			.status = PCT_STATUS_EMPTY, 
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

int pct_table_create(pct_table **p_pct, pct_table_create_info const *ci)
{
	pct_table *pct = new pct_table {};

	pct->page_create = ci->page_create;
	pct->page_size = ci->page_size;
	pct->capacity = ci->capacity;
	pct->usr = ci->usr;

	*p_pct = pct;

	return 0;
}

void pct_table_destroy(pct_table *pct)
{
	delete pct;
}

//------------------------------------------------------------------------------
// Compile time tests

static constexpr pct_state pct_state_test_val = {7,13,17,19};

static_assert(pct_state_unpack(pct_state_pack(pct_state_test_val)).status == 7);
static_assert(pct_state_unpack(pct_state_pack(pct_state_test_val)).flags == 13);
static_assert(pct_state_unpack(pct_state_pack(pct_state_test_val)).gen == 17);
static_assert(pct_state_unpack(pct_state_pack(pct_state_test_val)).refs == 19);
static_assert(pct_state_pack(pct_state_unpack(0xDEADBEEF)) == 0xDEADBEEF);

