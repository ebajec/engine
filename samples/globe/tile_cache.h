#ifndef TILE_CACHE_H
#define TILE_CACHE_H

#include "texture_loader.h"

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

union TileCode
{
	struct {
		uint8_t face : 3;
		uint8_t zoom : 5;
		uint64_t idx : 56;
	};
	uint64_t u64;

	constexpr bool operator == (const TileCode& other) const {
		return u64 == other.u64;
	}
};

struct TileCodeHash
{
	constexpr size_t operator()(const TileCode& code) const {
		return std::hash<uint64_t>{}(code.u64);
	}
};

struct TilePage
{
	std::vector<uint16_t> free_list;
	GLuint tex_array;
};

struct TileTexIndex
{
	GLuint array;
	uint16_t page_idx;
	uint16_t tex_idx;
};

class TileCache
{
	struct tex_idx_t
	{
		uint16_t page;
		uint16_t tex;
	};

	typedef std::list<std::pair<TileCode,tex_idx_t>> lru_list_t;

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
	tex_idx_t allocate();
	void deallocate(tex_idx_t idx);
	void reserve(uint32_t count);

	tex_idx_t evict_one();

	void insert(TileCode, tex_idx_t);

public:
	TileCache();

	void get_textures(
		const std::span<TileCode> tiles, 
		std::vector<TileTexIndex>& textures,
		std::vector<std::pair<TileCode,TileTexIndex>>& new_tiles
	);
};

#endif //TILE_CACHE_H
