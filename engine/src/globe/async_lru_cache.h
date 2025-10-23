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

enum alc_status : uint8_t
{
	ALC_STATUS_EMPTY,
	ALC_STATUS_READY,
	ALC_STATUS_LOADING,
	ALC_STATUS_QUEUED,
	ALC_STATUS_CANCELLED,
};

struct alignas(8) alc_state
{
	uint8_t status;
	uint8_t flags;
	uint16_t gen;
	uint32_t refs;
};

static_assert(sizeof(alc_state) == 8);

static const uint64_t ALC_STATE_STATUS_SHIFT = 8*offsetof(alc_state,status);
static const uint64_t ALC_STATE_FLAGS_SHIFT = 8*offsetof(alc_state,flags);
static const uint64_t ALC_STATE_GEN_SHIFT 	= 8*offsetof(alc_state,gen);
static const uint64_t ALC_STATE_REFS_SHIFT 	= 8*offsetof(alc_state,refs);

static const uint64_t ALC_STATE_STATUS_MASK 	= 
	(uint64_t)(((uint64_t)1 << 8*sizeof(((struct alc_state*)0)->status)) - 1)
	<< ALC_STATE_STATUS_SHIFT;
static const uint64_t ALC_STATE_FLAGS_MASK 	= 
	(uint64_t)(((uint64_t)1 << 8*sizeof(((struct alc_state*)0)->flags)) - 1)
	<< ALC_STATE_FLAGS_SHIFT;
static const uint64_t ALC_STATE_GEN_MASK 	= 
	(uint64_t)(((uint64_t)1 << 8*sizeof(((struct alc_state*)0)->gen)) - 1)
	<< ALC_STATE_GEN_SHIFT;
static const uint64_t ALC_STATE_REFS_MASK 	=
	(uint64_t)(((uint64_t)1 << 8*sizeof(((struct alc_state*)0)->refs)) - 1)
	<< ALC_STATE_REFS_SHIFT;

static inline constexpr uint64_t alc_state_pack(alc_state state)
{
	uint64_t bits = 0;	
	bits |= (uint64_t)state.status << ALC_STATE_STATUS_SHIFT;
	bits |= (uint64_t)state.flags << ALC_STATE_FLAGS_SHIFT;
	bits |= (uint64_t)state.gen << ALC_STATE_GEN_SHIFT;
	bits |= (uint64_t)state.refs << ALC_STATE_REFS_SHIFT;
	return bits;
}

static inline constexpr alc_status alc_state_status(uint64_t bits)
{
	return (alc_status)((bits & ALC_STATE_STATUS_MASK) >> ALC_STATE_STATUS_SHIFT);
}
static inline constexpr uint8_t alc_state_flags(uint64_t bits)
{
	return (uint8_t)((bits & ALC_STATE_FLAGS_MASK) >> ALC_STATE_FLAGS_SHIFT);
}
static inline constexpr uint16_t alc_state_gen(uint64_t bits)
{
	return (uint16_t)((bits & ALC_STATE_GEN_MASK) >> ALC_STATE_GEN_SHIFT);
}
static inline constexpr uint32_t alc_state_refs(uint64_t bits)
{
	return (uint32_t)((bits & ALC_STATE_REFS_MASK) >> ALC_STATE_REFS_SHIFT);
}

static inline constexpr alc_state alc_state_unpack(uint64_t bits)
{
	alc_state state = {
		.status = alc_state_status(bits),
		.flags = alc_state_flags(bits),
		.gen = alc_state_gen(bits),
		.refs = alc_state_refs(bits),
	};
	return state;
}

struct alc_index
{
	uint32_t page;
	uint32_t ent;

	bool is_valid() {
		return page != UINT32_MAX && ent != UINT32_MAX;
	}

	constexpr bool operator == (const alc_index& other) const {
		return page == other.page && ent == other.ent;
	}
};

struct alc_index_hash
{
	size_t operator()(const alc_index& idx) const {
		uint64_t u64 = ((uint64_t)idx.page << 32) | (uint64_t)(idx.ent);
		return std::hash<uint64_t>()(u64);
	}
};

static const alc_index ALC_INDEX_NONE = {UINT32_MAX, UINT32_MAX};

typedef uint64_t alc_page_handle_t;
typedef std::atomic_uint64_t alc_atomic_state;
typedef int(*alc_page_create)(void*, alc_page_handle_t*);
typedef int(*alc_page_destroy)(void*, alc_page_handle_t);
typedef std::list<alc_index> alc_lru_list;

struct alc_entry 
{
	uint64_t key;
	alc_atomic_state state;
};

struct alc_page
{
	alc_page_handle_t handle;
	std::vector<uint32_t> free_list;
	alc_entry *entries;
};

struct alc_result
{
	alc_index idx;
	alc_entry *p_ent;
	bool needs_load;
	bool is_ready;
};

struct alc_create_info
{
	size_t capacity;
	size_t page_size;

	void *usr;
	alc_page_create page_create;
	alc_page_destroy page_destroy;
};

struct alc_table;

extern int alc_create(alc_table **p_alc, alc_create_info const *ci);
extern void alc_destroy(alc_table *alc);

/// @brief If key does not exist in the table, attempts to evict an existing 
/// entry and allocate space for the new value.  If the key exists, the result
/// will indicate whether it is ready.  If the key did not exist but an entry
/// was reserved, the result will always indicate that a load is needed.
///
/// @note This function is not thread safe. 
extern alc_result alc_get(alc_table *alc, uint64_t key);

extern alc_entry *alc_acquire(alc_table *alc, uint64_t key);
extern void alc_release(alc_entry *ent);

struct alc_table
{
	alc_lru_list lru;

	std::unordered_map<
		uint64_t, 
		alc_lru_list::iterator 
	> map;

	std::priority_queue<
		uint16_t, 
		std::vector<uint16_t>, 
		std::greater<uint16_t>
	> open_pages;

	std::vector<alc_page> pages;

	size_t page_size;
	size_t capacity;

	void *usr;
	alc_page_create page_create;
	alc_page_destroy page_destroy;
};

#endif //CT_TABLE_H
