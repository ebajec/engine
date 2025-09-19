#ifndef DATA_SOURCE_H
#define DATA_SOURCE_H

#include "engine/globe/tiling.h"
#include "engine/globe/paged_cache_table.h"

static constexpr double DATA_SOURCE_TEST_AMP = 0.1;
static constexpr double DATA_SOURCE_TEST_FREQ = 12;

//------------------------------------------------------------------------------
// Data source

typedef void(*TileLoaderFunc)(TileCode code, void* dst, void* usr, const pct_atomic_state* p_state);

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

#endif
