#include "ct_table.h"

#include "utils/log.h"

#include <cassert>
#include <cstdlib>

static int ct_create_page(ct_table *ct, ct_page *page)
{
	size_t size = ct->page_size;
	page->free_list.resize(size);

	for (size_t i = 0; i < size; ++i) {
		page->free_list[i] = static_cast<uint32_t>(size - i - 1);
	}

	page->entries = (ct_entry*)calloc(size, sizeof(ct_entry));

	return ct->page_create(ct->usr, &page->handle);
}

static ct_index ct_allocate(ct_table *ct)
{
	if (ct->open_pages.empty()) {
		ct->pages.emplace_back();

		if (ct_create_page(ct, &ct->pages.back()) < 0)
			return CT_INDEX_NONE;

		ct->open_pages.push(static_cast<uint16_t>(ct->pages.size() - 1));
	}
	
	uint16_t page = ct->open_pages.top(); 

	assert(page < ct->pages.size());

	ct_page *p_page = &ct->pages[page];

	uint32_t ent = p_page->free_list.back();

	assert(ent < ct->page_size);

	p_page->free_list.pop_back();

	if (p_page->free_list.empty()) {
		ct->open_pages.pop();
	}

	return ct_index{
		.page = page,
		.ent = ent,
	};
}

static ct_index ct_evict_one(ct_table *ct)
{
	ct_index idx = ct->lru.back(); 
	ct_entry *ent = &ct->pages[idx.page].entries[idx.ent];

	uint64_t state_packed = ent->state.load();
	ct_state desired = {
		.status = CT_STATUS_EMPTY, 
		.flags = 0,
		.gen = 0,
		.refs = 0
	};

	do {
		ct_state state = ct_state_unpack(state_packed);
		if (state.refs > 0) {
			log_info("Could not evict entry %d from cache; in use",ent->key);
			return CT_INDEX_NONE;
		}

		if (state.status == CT_STATUS_CANCELLED) {
			return CT_INDEX_NONE;
		}

		if (state.status == CT_STATUS_LOADING || 
			state.status == CT_STATUS_QUEUED)
			desired = {
				.status = CT_STATUS_CANCELLED, 
				.flags = 0,
				.gen = 0,
				.refs = state.refs
			};
		else 
			desired = {
				.status = CT_STATUS_EMPTY,
				.flags = 0,
				.gen = 0,
				.refs = 0
			};
	} while (!ent->state.compare_exchange_weak(state_packed, ct_state_pack(desired)));

	if (desired.status == CT_STATUS_CANCELLED) {
		log_info("Cancelled load for tile %d",ent->key);
		return CT_INDEX_NONE;
	}

	if (ct->map.find(ent->key) == ct->map.end()) {
		log_error("Failed to evict entry at %d; not contained in table!",ent->key);
		return CT_INDEX_NONE;
	}

	//log_info("Evicted tile %d from CPU cache",ent->code);

	ct->map.erase(ent->key);	
	ct->lru.pop_back();

	return idx;
}

ct_load_result ct_table_load(ct_table *ct, uint64_t key)
{
	ct_load_result res {};

	auto it = ct->map.find(key);
	if (it != ct->map.end()) {
		ct_lru_list::iterator l_ent = it->second;
		ct->lru.splice(ct->lru.begin(), ct->lru, l_ent);

		ct_index idx = *it->second;
		ct_entry *ent = &ct->pages[idx.page].entries[idx.ent];
		ct_state state = ct_state_unpack(ent->state.load()); 

		if (state.status == CT_STATUS_EMPTY) {
			if (state.refs > 0) 
				log_error("Empty tile has %d references",state.refs);

			res.idx = idx;
			res.p_ent = ent;
			res.needs_load = true;

		} else if (state.status == CT_STATUS_READY) {
			res.is_ready = true;
		}
	} else {
#if 1 
		ct_index idx = ct->map.size() >= ct->capacity ?
	  		ct_evict_one(ct) : ct_allocate(ct);

		if (!idx.is_valid()) 
			return res;

		ct->lru.push_front(idx);
		ct->map[key] = ct->lru.begin();

		ct_entry *ent = &ct->pages[idx.page].entries[idx.ent];
		ent->key = key,
		ent->state = ct_state_pack({
			.status = CT_STATUS_EMPTY, 
			.flags = 0,
			.gen = 0,
			.refs = 0
		}),

		res.idx = idx;
		res.p_ent = ent;
		res.needs_load = true;
#endif
	}

	return res;
}

int ct_table_create(ct_table **p_ct, ct_table_create_info const *ci)
{
	ct_table *ct = new ct_table {};

	ct->page_create = ci->page_create;
	ct->page_size = ci->page_size;
	ct->capacity = ci->capacity;
	ct->usr = ci->usr;

	*p_ct = ct;

	return 0;
}

void ct_table_destroy(ct_table *ct)
{
	delete ct;
}

//------------------------------------------------------------------------------
// Compile time tests

static constexpr ct_state ct_state_test_val = {7,13,17,19};

static_assert(ct_state_unpack(ct_state_pack(ct_state_test_val)).status == 7);
static_assert(ct_state_unpack(ct_state_pack(ct_state_test_val)).flags == 13);
static_assert(ct_state_unpack(ct_state_pack(ct_state_test_val)).gen == 17);
static_assert(ct_state_unpack(ct_state_pack(ct_state_test_val)).refs == 19);
static_assert(ct_state_pack(ct_state_unpack(0xDEADBEEF)) == 0xDEADBEEF);

