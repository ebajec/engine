#ifndef TERRAIN_H
#define TERRAIN_H

#include <globe/data_source.h>

#include "tile_cache.h"

struct CPUTileCache
{
	ds_context *ds;
	tc_cache *tc;

	int m_debug_zoom = 8;

	static CPUTileCache *create();
	~CPUTileCache();

	float sample_elevation_at(glm::dvec2 uv, uint8_t f) const;
	float sample_elevation_at(glm::dvec3 p) const;

	float tile_min(TileCode tile) const;
	float tile_max(TileCode tile) const;

	float min() const;
	float max() const;
};

#endif //TERRAIN_H
