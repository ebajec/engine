#ifndef TERRAIN_H
#define TERRAIN_H

#include <globe/data_source.h>

#include "tile_cache.h"
#include "minmax_tree.h"

#include <mutex>
#include <vector>

struct mmt_update
{
	float min, max;
	uint64_t id;
};

struct CPUTileCache
{
	// TODO : Pair up data sources and caches
	ds_context *ds;
	tc_cache *tc;

	mmt_tree *mmt;

	std::mutex sync;
	std::vector<mmt_update> updates;
	std::vector<mmt_update> working;

	int m_debug_zoom = 8;

	static CPUTileCache *create();
	~CPUTileCache();

	void load_tiles(size_t count, const TileCode *tiles, TileCode *out);

	float sample_elevation_at(glm::dvec2 uv, uint8_t f) const;
	float sample_elevation_at(glm::dvec3 p) const;

	std::pair<float,float> tile_minmax(TileCode tile) const;

	float min() const;
	float max() const;
};

#endif //TERRAIN_H
