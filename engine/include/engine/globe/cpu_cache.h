#ifndef CPU_CACHE_H
#define CPU_CACHE_H

#include "engine/globe/tiling.h"
#include "engine/globe/paged_cache_table.h"
#include "engine/globe/data_source.h"

#include <memory>
#include <optional>
#include <span>

static constexpr size_t TILE_CPU_PAGE_SIZE = 32;

struct TileDataRef
{
	uint8_t *data;
	size_t size;
	pct_atomic_state *p_state;
};

struct TileCPUCache
{
	std::unique_ptr<pct_table,decltype(&pct_table_destroy)> m_ct{
		nullptr, &pct_table_destroy};

	size_t m_tile_size;
	size_t m_tile_cap = 1 << 14;
private:

	TileCode find_best(TileCode code);
	uint8_t *get_block(pct_index idx) const;
public:
	static TileCPUCache *create(size_t tile_size, size_t capacity);

	std::vector<TileCode> update(const TileDataSource& source, 
							  const std::span<TileCode> tiles);

	std::optional<TileDataRef> acquire_block(TileCode code) const;
	void release_block(TileDataRef ref) const;

	const uint8_t *acquire_block(TileCode code, size_t *size, 
						  pct_atomic_state **p_state) const;
};

#endif // CPU_CACHE_H
