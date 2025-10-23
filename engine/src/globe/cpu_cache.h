#ifndef CPU_CACHE_H
#define CPU_CACHE_H

#include "engine/globe/tiling.h"
#include "engine/globe/data_source.h"

#include "globe/async_lru_cache.h"
#include "globe/tile_cache.h"

#include <memory>
#include <optional>

static constexpr size_t TILE_CPU_PAGE_SIZE = 32;

struct TileCacheSegment
{
	std::unique_ptr<alc_table,decltype(&alc_destroy)> alc{
		nullptr, &alc_destroy};

	size_t tile_size;
	size_t tile_cap = 1 << 14;
private:

	TileCode find_best(TileCode code);
	uint8_t *get_block(alc_index idx) const;
public:
	static TileCacheSegment *create(size_t tile_size, size_t capacity);
	~TileCacheSegment();

	void update(TileDataSource const *source, size_t count,
				 TileCode const *tiles, TileCode *out);

	std::optional<tc_ref> acquire(TileCode code) const;
	void release_block(tc_ref ref) const;

	const uint8_t *acquire_block(TileCode code, size_t *size, 
						  alc_atomic_state **p_state) const;
};

#endif // CPU_CACHE_H
