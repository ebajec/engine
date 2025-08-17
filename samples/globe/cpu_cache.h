#ifndef DATASET_H
#define DATASET_H

#include "tiling.h"

#include <unordered_map>
#include <list>
#include <queue>
#include <memory>
#include <atomic>

static constexpr uint32_t TILE_WIDTH = 256;
static constexpr uint32_t TILE_SIZE = TILE_WIDTH*TILE_WIDTH;
static constexpr size_t TILE_CPU_PAGE_SIZE = 32;

enum TileCPULoadStatus
{
	TILE_CPU_STATE_EMPTY,
	TILE_DATA_STATE_READY,
	TILE_DATA_STATE_LOADING,
	TILE_DATA_STATE_QUEUED,
	TILE_DATA_STATE_CANCELLED,
};

union TileCPULoadState
{
	struct alignas(8) {
		TileCPULoadStatus status;
		uint32_t refs;
	};
	uint64_t u64;
};

typedef void(*TileLoaderFunc)(TileCode code, void* dst, void* usr, const std::atomic<TileCPULoadState>* p_state);

struct TileDataSource
{
	std::unordered_map<
		TileCode, 
		uint32_t, 
		TileCodeHash
	> m_data;

	int m_debug_zoom = 0;

	TileLoaderFunc m_loader;
	void* m_usr;

	static TileDataSource *create(TileLoaderFunc loader = nullptr, 
							   void* usr = nullptr);

	TileCode find(TileCode code) const;
	void load(TileCode code, void* dst) const;

	float sample_elevation_at(glm::dvec2 uv, uint8_t f) const;
	float sample_elevation_at(glm::dvec3 p) const;
};

struct TileDataRef
{
	uint8_t *data;
	size_t size;
	std::atomic<TileCPULoadState> *p_state;
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

struct TileCPUCache
{
	typedef std::list<TileCPUIndex> lru_list_t;

	std::unordered_map<
		TileCode, 
		lru_list_t::iterator, 
		TileCodeHash
	> m_map;
	lru_list_t m_lru;

	struct entry_t
	{
		TileCode code;
		std::atomic<TileCPULoadState> state;
	};

	struct page_t
	{
		entry_t entries[TILE_CPU_PAGE_SIZE];
		std::vector<uint32_t> free_list;
		std::unique_ptr<uint8_t[]> mem;
	};

	std::priority_queue<
		uint16_t, 
		std::vector<uint16_t>, 
		std::greater<uint16_t>
	> m_open_pages;

	std::vector<std::unique_ptr<page_t>> m_pages;

	size_t m_tile_size;
	size_t m_tile_cap = 1 << 14;
private:
	TileCPUIndex evict_one();	
	TileCPUIndex allocate();

	TileCode find_best(TileCode code);
	entry_t *get_entry(TileCPUIndex idx) const;
	uint8_t *get_block(TileCPUIndex idx) const;
public:
	static TileCPUCache *create(size_t tile_size, size_t capacity);

	std::vector<TileCode> update(
		const TileDataSource& source, 
		const std::span<TileCode> tiles);
	const uint8_t *acquire_block(TileCode code, size_t *size, 
						  std::atomic<TileCPULoadState> **p_state) const;

	std::optional<TileDataRef> acquire_block(TileCode code) const;
	void release_block(TileDataRef ref) const;
};

#endif // DATASET_H
