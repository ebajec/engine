#include "dataset.h"
#include "tiling.h"
#include "utils/log.h"

#include <imgui.h>

TileDataCache TileDataCache::create(TileLoaderFunc loader, void* usr, size_t tile_size)
{
	TileDataCache cache {};
	cache.m_block_size = tile_size;
	cache.m_loader = loader;
	cache.m_loader_usr = usr;
	cache.m_blocks.reset(new uint8_t[cache.m_block_cap*cache.m_block_size]);
	return cache;
}

size_t TileDataCache::evict()
{
	entry_t ent = m_lru.back();
	m_lru.pop_back();
	m_map.erase(ent.code);	
	return ent.block;
}

const uint8_t *TileDataCache::get_block(TileCode code, size_t *size) const
{
	auto it = m_map.find(code);

	if (it == m_map.end()) {
		log_error("TileDataCache::get_block : Failed to find tile with id %ld",
			code.u64);
	}

	if (size) *size = m_block_size;
	return &m_blocks[it->second->block];
}

std::vector<TileCode> TileDataCache::load(const std::span<TileCode> tiles)
{
	auto end = m_data.end();

	std::vector<TileCode> loaded (tiles.size());

	ImGui::SliderInt("Max zoom", &m_debug_zoom, 0, 24);

	for (size_t i = 0; i < tiles.size(); ++i) {
		TileCode code = tiles[i];

		auto it = m_data.find(code);

		while (code.zoom > m_debug_zoom && it == end) {
			code.idx >>= 2;
			--code.zoom;

			it = m_data.find(code);
		}
		loaded[i] = (it == end) ? code : it->first;
	}

	std::vector<std::pair<TileCode, uint8_t*>> loads;

	for (TileCode code : loaded) {
		auto it = m_map.find(code);
		if (it != m_map.end()) {
			lru_list_t::iterator ent = it->second;
			m_lru.splice(m_lru.begin(), m_lru, ent);
		} else {
			size_t block = m_block_idx < m_block_cap ? 
				(m_block_idx++)*m_block_size : evict();	

			m_lru.push_front({
				.code = code,
				.block = block 
			});
			m_map[code] = m_lru.begin();

			loads.push_back({code,&m_blocks[block]});
		}
	}

	for (auto pair : loads) {
		m_loader(pair.first, pair.second, m_loader_usr);
	}

	return loaded;
}

