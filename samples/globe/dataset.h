#ifndef DATASET_H
#define DATASET_H

#include "tiling.h"

#include <unordered_map>
#include <list>
#include <memory>

typedef void(*TileLoaderFunc)(TileCode code, void* dst, void* usr);

struct TileDataCache
{
	struct entry_t
	{
		TileCode code;
		size_t block;
	};

	typedef std::list<entry_t> lru_list_t;

	std::unordered_map<
		TileCode, 
		lru_list_t::iterator, 
		TileCodeHash
	> m_map;

	lru_list_t m_lru;

	std::unique_ptr<uint8_t[]> m_blocks;
	size_t m_block_idx = 0;
	size_t m_block_cap = 4096;
	size_t m_block_size;

	std::unordered_map<
		TileCode, 
		uint32_t, 
		TileCodeHash
	> m_data;

	int m_debug_zoom = 0;

	TileLoaderFunc m_loader;
	void* m_loader_usr;
private:
	size_t evict();	
	size_t allocate();
public:
	static TileDataCache create(TileLoaderFunc loader, void* usr, size_t tile_size);
	std::vector<TileCode> load(const std::span<TileCode> tiles);
	const uint8_t *get_block(TileCode code, size_t *size) const;
};


#endif // DATASET_H
