#ifndef CT_TABLE_H
#define CT_TABLE_H

#include <atomic>
#include <vector>
#include <list>
#include <queue>
#include <unordered_map>
#include <functional>

#include <cstddef>
#include <cstdint>

enum pct_status : uint8_t
{
	PCT_STATUS_EMPTY,
	PCT_STATUS_READY,
	PCT_STATUS_LOADING,
	PCT_STATUS_QUEUED,
	PCT_STATUS_CANCELLED,
};

struct alignas(8) pct_state
{
	uint8_t status;
	uint8_t flags;
	uint16_t gen;
	uint32_t refs;
};

static_assert(sizeof(pct_state) == 8);

static const uint64_t PCT_STATE_STATUS_SHIFT = 8*offsetof(pct_state,status);
static const uint64_t PCT_STATE_FLAGS_SHIFT 	= 8*offsetof(pct_state,flags);
static const uint64_t PCT_STATE_GEN_SHIFT 	= 8*offsetof(pct_state,gen);
static const uint64_t PCT_STATE_REFS_SHIFT 	= 8*offsetof(pct_state,refs);

static const uint64_t PCT_STATE_STATUS_MASK 	= 
	(uint64_t)(((uint64_t)1 << 8*sizeof(((struct pct_state*)0)->status)) - 1)
	<< PCT_STATE_STATUS_SHIFT;
static const uint64_t PCT_STATE_FLAGS_MASK 	= 
	(uint64_t)(((uint64_t)1 << 8*sizeof(((struct pct_state*)0)->flags)) - 1)
	<< PCT_STATE_FLAGS_SHIFT;
static const uint64_t PCT_STATE_GEN_MASK 	= 
	(uint64_t)(((uint64_t)1 << 8*sizeof(((struct pct_state*)0)->gen)) - 1)
	<< PCT_STATE_GEN_SHIFT;
static const uint64_t PCT_STATE_REFS_MASK 	=
	(uint64_t)(((uint64_t)1 << 8*sizeof(((struct pct_state*)0)->refs)) - 1)
	<< PCT_STATE_REFS_SHIFT;

static inline constexpr uint64_t pct_state_pack(pct_state state)
{
	uint64_t bits = 0;	
	bits |= (uint64_t)state.status << PCT_STATE_STATUS_SHIFT;
	bits |= (uint64_t)state.flags << PCT_STATE_FLAGS_SHIFT;
	bits |= (uint64_t)state.gen << PCT_STATE_GEN_SHIFT;
	bits |= (uint64_t)state.refs << PCT_STATE_REFS_SHIFT;
	return bits;
}

static inline constexpr pct_status pct_state_status(uint64_t bits)
{
	return (pct_status)((bits & PCT_STATE_STATUS_MASK) >> PCT_STATE_STATUS_SHIFT);
}
static inline constexpr uint8_t pct_state_flags(uint64_t bits)
{
	return (uint8_t)((bits & PCT_STATE_FLAGS_MASK) >> PCT_STATE_FLAGS_SHIFT);
}
static inline constexpr uint16_t pct_state_gen(uint64_t bits)
{
	return (uint16_t)((bits & PCT_STATE_GEN_MASK) >> PCT_STATE_GEN_SHIFT);
}
static inline constexpr uint32_t pct_state_refs(uint64_t bits)
{
	return (uint32_t)((bits & PCT_STATE_REFS_MASK) >> PCT_STATE_REFS_SHIFT);
}

static inline constexpr pct_state pct_state_unpack(uint64_t bits)
{
	pct_state state = {
		.status = pct_state_status(bits),
		.flags = pct_state_flags(bits),
		.gen = pct_state_gen(bits),
		.refs = pct_state_refs(bits),
	};
	return state;
}

struct pct_index
{
	uint32_t page;
	uint32_t ent;

	bool is_valid() {
		return page != UINT32_MAX && ent != UINT32_MAX;
	}

	constexpr bool operator == (const pct_index& other) const {
		return page == other.page && ent == other.ent;
	}
};

struct pct_index_hash
{
	constexpr size_t operator()(const pct_index& idx) const {
		uint64_t u64 = ((uint64_t)idx.page << 32) | (uint64_t)(idx.ent);
		return std::hash<uint64_t>()(u64);
	}
};

static const pct_index PCT_INDEX_NONE = {UINT32_MAX, UINT32_MAX};

typedef uint64_t pct_page_handle_t;
typedef std::atomic_uint64_t pct_atomic_state;
typedef int(*pct_page_func)(void*, pct_page_handle_t*);
typedef std::list<pct_index> pct_lru_list;

struct pct_entry 
{
	uint64_t key;
	pct_atomic_state state;
};

struct pct_page
{
	pct_page_handle_t handle;
	std::vector<uint32_t> free_list;
	pct_entry *entries;
};

struct pct_table
{
	pct_lru_list lru;

	std::unordered_map<
		uint64_t, 
		pct_lru_list::iterator 
	> map;

	std::priority_queue<
		uint16_t, 
		std::vector<uint16_t>, 
		std::greater<uint16_t>
	> open_pages;

	std::vector<pct_page> pages;

	size_t page_size;
	size_t capacity;

	void *usr;
	pct_page_func page_create;
};

struct pct_load_result
{
	pct_index idx;
	pct_entry *p_ent;
	bool needs_load;
	bool is_ready;
};

struct pct_table_create_info
{
	size_t capacity;
	size_t page_size;

	void *usr;
	pct_page_func page_create;
};

extern int pct_table_create(pct_table **p_pct, pct_table_create_info const *ci);
extern void pct_table_destroy(pct_table *pct);

extern pct_load_result pct_table_load(pct_table *ct_table, uint64_t key);

#endif //CT_TABLE_H
