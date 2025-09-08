#ifndef DATASET_H
#define DATASET_H

#include "tiling.h"

#include <unordered_map>
#include <list>
#include <queue>
#include <memory>
#include <atomic>
#include <optional>
#include <span>

static constexpr uint32_t TILE_WIDTH = 256;
static constexpr uint32_t TILE_SIZE = TILE_WIDTH*TILE_WIDTH;
static constexpr size_t TILE_CPU_PAGE_SIZE = 32;

//------------------------------------------------------------------------------
// Source

enum TileCPULoadStatus
{
	TILE_CPU_STATE_EMPTY,
	TILE_CPU_STATE_READY,
	TILE_CPU_STATE_LOADING,
	TILE_CPU_STATE_QUEUED,
	TILE_CPU_STATE_CANCELLED,
};

union TileCPULoadState
{
	struct alignas(8) {
		TileCPULoadStatus status;
		uint32_t refs;
	};
	uint64_t u64;
};

struct TileDataRef
{
	uint8_t *data;
	size_t size;
	std::atomic_uint64_t *p_state;
};

struct TileCPUIndex
{
	uint32_t page;
	uint32_t ent;

	bool is_valid() {
		return page != UINT32_MAX && ent != UINT32_MAX;
	}
};

static constexpr TileCPUIndex TILE_CPU_INDEX_NONE = {UINT32_MAX,UINT32_MAX};

struct TileDataSource;

//------------------------------------------------------------------------------
// begin ct

enum ct_status : uint8_t
{
	CT_STATUS_EMPTY,
	CT_STATUS_READY,
	CT_STATUS_LOADING,
	CT_STATUS_QUEUED,
	CT_STATUS_CANCELLED,
};

struct alignas(8) ct_state
{
	uint8_t status;
	uint8_t flags;
	uint16_t gen;
	uint32_t refs;
};

static_assert(sizeof(ct_state) == 8);

static const uint64_t CT_STATE_STATUS_SHIFT = 8*offsetof(ct_state,status);
static const uint64_t CT_STATE_FLAGS_SHIFT 	= 8*offsetof(ct_state,flags);
static const uint64_t CT_STATE_GEN_SHIFT 	= 8*offsetof(ct_state,gen);
static const uint64_t CT_STATE_REFS_SHIFT 	= 8*offsetof(ct_state,refs);

static const uint64_t CT_STATE_STATUS_MASK 	= 
	(uint64_t)(((uint64_t)1 << 8*sizeof(((struct ct_state*)0)->status)) - 1) << CT_STATE_STATUS_SHIFT;
static const uint64_t CT_STATE_FLAGS_MASK 	= 
	(uint64_t)(((uint64_t)1 << 8*sizeof(((struct ct_state*)0)->flags)) - 1) << CT_STATE_FLAGS_SHIFT;
static const uint64_t CT_STATE_GEN_MASK 	= 
	(uint64_t)(((uint64_t)1 << 8*sizeof(((struct ct_state*)0)->gen)) - 1) << CT_STATE_GEN_SHIFT;
static const uint64_t CT_STATE_REFS_MASK 	=
	(uint64_t)(((uint64_t)1 << 8*sizeof(((struct ct_state*)0)->refs)) - 1) << CT_STATE_REFS_SHIFT;

static inline constexpr uint64_t ct_state_pack(ct_state state)
{
	uint64_t bits = 0;	
	bits |= (uint64_t)state.status << CT_STATE_STATUS_SHIFT;
	bits |= (uint64_t)state.flags << CT_STATE_FLAGS_SHIFT;
	bits |= (uint64_t)state.gen << CT_STATE_GEN_SHIFT;
	bits |= (uint64_t)state.refs << CT_STATE_REFS_SHIFT;
	return bits;
}

static inline constexpr ct_status ct_state_status(uint64_t bits)
{
	return (ct_status)((bits & CT_STATE_STATUS_MASK) >> CT_STATE_STATUS_SHIFT);
}
static inline constexpr uint8_t ct_state_flags(uint64_t bits)
{
	return (uint8_t)((bits & CT_STATE_FLAGS_MASK) >> CT_STATE_FLAGS_SHIFT);
}
static inline constexpr uint16_t ct_state_gen(uint64_t bits)
{
	return (uint16_t)((bits & CT_STATE_GEN_MASK) >> CT_STATE_GEN_SHIFT);
}
static inline constexpr uint32_t ct_state_refs(uint64_t bits)
{
	return (uint32_t)((bits & CT_STATE_REFS_MASK) >> CT_STATE_REFS_SHIFT);
}

static inline constexpr ct_state ct_state_unpack(uint64_t bits)
{
	ct_state state = {
		.status = ct_state_status(bits),
		.flags = ct_state_flags(bits),
		.gen = ct_state_gen(bits),
		.refs = ct_state_refs(bits),
	};
	return state;
}

static constexpr ct_state ct_state_test_val = {7,13,17,19};

static_assert(ct_state_unpack(ct_state_pack(ct_state_test_val)).status == 7);
static_assert(ct_state_unpack(ct_state_pack(ct_state_test_val)).flags == 13);
static_assert(ct_state_unpack(ct_state_pack(ct_state_test_val)).gen == 17);
static_assert(ct_state_unpack(ct_state_pack(ct_state_test_val)).refs == 19);
static_assert(ct_state_pack(ct_state_unpack(0xDEADBEEF)) == 0xDEADBEEF);

struct ct_index
{
	uint32_t page;
	uint32_t ent;

	bool is_valid() {
		return page != UINT32_MAX && ent != UINT32_MAX;
	}

	constexpr bool operator == (const ct_index& other) const {
		return page == other.page && ent == other.ent;
	}
};

struct ct_index_hash_t
{
	constexpr size_t operator()(const ct_index& idx) const {
		uint64_t u64 = ((uint64_t)idx.page << 32) | (uint64_t)(idx.ent);
		return std::hash<uint64_t>()(u64);
	}
};

static const ct_index CT_INDEX_NONE = {UINT32_MAX, UINT32_MAX};

typedef uint64_t ct_page_handle_t;
typedef std::atomic_uint64_t ct_atomic_state;
typedef int(*ct_page_func)(void*, ct_page_handle_t*);
typedef std::list<ct_index> ct_lru_list;

struct ct_entry 
{
	uint64_t key;
	ct_atomic_state state;
};

struct ct_page
{
	ct_page_handle_t handle;
	std::vector<uint32_t> free_list;
	ct_entry *entries;
};

struct ct_table
{
	ct_lru_list lru;

	std::unordered_map<
		uint64_t, 
		ct_lru_list::iterator 
	> map;

	std::priority_queue<
		uint16_t, 
		std::vector<uint16_t>, 
		std::greater<uint16_t>
	> open_pages;

	std::vector<ct_page> pages;

	size_t page_size;
	size_t capacity;

	void *usr;
	ct_page_func page_create;
};

struct ct_load_result
{
	ct_index idx;
	ct_entry *p_ent;
	bool needs_load;
	bool is_ready;
};

struct ct_table_create_info
{
	size_t capacity;
	size_t page_size;

	void *usr;
	ct_page_func page_create;
};

extern int ct_table_create(ct_table **p_ct, ct_table_create_info const *ci);
extern void ct_table_destroy(ct_table *ct);

extern ct_load_result ct_table_load(ct_table *ct_table, uint64_t key);

//------------------------------------------------------------------------------
// new

struct TileCPUCache
{
	std::unique_ptr<ct_table,decltype(&ct_table_destroy)> m_ct{
		nullptr, &ct_table_destroy};

	size_t m_tile_size;
	size_t m_tile_cap = 1 << 14;
private:

	TileCode find_best(TileCode code);
	uint8_t *get_block(ct_index idx) const;
public:
	static TileCPUCache *create(size_t tile_size, size_t capacity);

	std::vector<TileCode> update(const TileDataSource& source, const std::span<TileCode> tiles);
	std::optional<TileDataRef> acquire_block(TileCode code) const;
	void release_block(TileDataRef ref) const;

	const uint8_t *acquire_block(TileCode code, size_t *size, 
						  ct_atomic_state **p_state) const;
};

//------------------------------------------------------------------------------
// Data source

typedef void(*TileLoaderFunc)(TileCode code, void* dst, void* usr, const ct_atomic_state* p_state);

struct TileDataSource
{
	std::unordered_map<
		TileCode, 
		uint32_t, 
		TileCodeHash
	> m_data;

	int m_debug_zoom = 8;

	TileLoaderFunc m_loader;
	void* m_usr;

	static TileDataSource *create(TileLoaderFunc loader = nullptr, 
							   void* usr = nullptr);

	TileCode find(TileCode code) const;
	void load(TileCode code, void* dst) const;

	float sample_elevation_at(glm::dvec2 uv, uint8_t f) const;
	float sample_elevation_at(glm::dvec3 p) const;
};



#endif // DATASET_H
