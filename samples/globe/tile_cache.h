#ifndef TILE_CACHE_H
#define TILE_CACHE_H

#include "texture_loader.h"
#include "tiling.h"

// STL
#include <unordered_map>
#include <queue>
#include <list>
#include <span>

// libc
#include <cstdint>

static constexpr uint32_t TILE_SIZE = 256;
static constexpr uint32_t TILE_PAGE_SIZE = 64;
static constexpr uint32_t MAX_TILE_PAGES = 16;
static constexpr uint32_t MAX_TILES = TILE_PAGE_SIZE*MAX_TILE_PAGES;

struct TilePage
{
	std::vector<uint16_t> free_list;
	GLuint tex_array;
};

struct TileTexIndex
{
	alignas(4)
	uint16_t page;
	uint16_t tex;
};

class TileTexCache
{
	typedef std::list<std::pair<TileCode,TileTexIndex>> lru_list_t;

	// TODO : Robin hood hash table instead of this
	lru_list_t m_lru;
	std::unordered_map<
		TileCode, 
		lru_list_t::iterator,
		TileCodeHash
	> m_map;

	std::priority_queue<
		uint16_t, 
		std::vector<uint16_t>, 
		std::greater<uint16_t>
	> m_open_pages;

	std::vector<TilePage> m_pages;

private:
	TileTexIndex allocate();
	void deallocate(TileTexIndex idx);
	void reserve(uint32_t count);

	TileTexIndex evict_one();

	void insert(TileCode, TileTexIndex);

public:
	TileTexCache();

	void get_textures(
		const std::span<TileCode> tiles, 
		std::vector<TileTexIndex>& textures,
		std::vector<std::pair<TileCode,TileTexIndex>>& new_tiles
	);

	void bind_texture_arrays(uint32_t base) const;
};

#endif //TILE_CACHE_H
