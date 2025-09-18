#ifndef DATASET_H
#define DATASET_H

#include "tiling.h"
#include "ct_table.h"

#include <unordered_map>
#include <memory>
#include <optional>
#include <span>

static constexpr double TILE_CACHE_DUMMY_AMP = 0.1;
static constexpr double TILE_CACHE_DUMMY_FREQ = 12;

static constexpr uint32_t TILE_WIDTH = 256;
static constexpr uint32_t TILE_SIZE = TILE_WIDTH*TILE_WIDTH;

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

//------------------------------------------------------------------------------
// Source

static constexpr size_t TILE_CPU_PAGE_SIZE = 32;

struct TileDataRef
{
	uint8_t *data;
	size_t size;
	ct_atomic_state *p_state;
};

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

#endif // DATASET_H
