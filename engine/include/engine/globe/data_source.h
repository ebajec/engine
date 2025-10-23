#ifndef DATA_SOURCE_H
#define DATA_SOURCE_H

#include "engine/globe/tiling.h"

//------------------------------------------------------------------------------
// Loader interface

struct ds_token;

struct ds_token_vtbl
{
	int (*is_cancelled)(struct ds_token *token);
};

struct ds_token
{
	void *usr;
	const struct ds_token_vtbl *vtbl;
};

struct ds_buf
{
	void *dst;
	size_t cap;
};

typedef int (*ds_load_fn)(
	void *usr, 
	uint64_t id,
	struct ds_buf *buf,
	struct ds_token *token
);

//------------------------------------------------------------------------------
// Data source

struct TileDataSource
{
	std::unordered_map<
		TileCode, 
		uint32_t, 
		TileCodeHash
	> m_data;


	ds_load_fn m_loader;
	void* m_usr;
	int m_debug_zoom = 8;

	uint64_t m_id;

	static constexpr double TEST_AMP = 0.1;
	static constexpr double TEST_FREQ = 12;

	uint64_t id() const {
		return m_id;
	}

	static TileDataSource *create(ds_load_fn loader = nullptr, void* usr = nullptr);

	TileCode find(TileCode code) const;

	float sample_elevation_at(glm::dvec2 uv, uint8_t f) const;
	float sample_elevation_at(glm::dvec3 p) const;

	float tile_min(TileCode tile) const;
	float tile_max(TileCode tile) const;

	float min() const;
	float max() const;
};

#endif
