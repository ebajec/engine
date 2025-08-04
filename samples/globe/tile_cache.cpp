#include "tile_cache.h"
#include "utils/log.h"

TilePage create_page()
{
	TilePage page{};

	glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &page.tex_array);
	glTextureStorage3D(
		page.tex_array, 
		1, 
		GL_R32F, 
		TILE_SIZE, 
		TILE_SIZE, 
		TILE_PAGE_SIZE
	);

	glTextureParameteri(page.tex_array, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(page.tex_array, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(page.tex_array, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(page.tex_array, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

	page.free_list.resize(TILE_PAGE_SIZE);

	for (uint16_t i = 0; i < TILE_PAGE_SIZE; ++i) {
		page.free_list[i] = TILE_PAGE_SIZE - i - 1;
	}

	return page;
}

void destroy_page(TilePage &page)
{
	glDeleteTextures(1,&page.tex_array);
}

TileTexIndex TileTexCache::allocate()
{
	if (m_open_pages.empty()) {
		m_pages.emplace_back(std::move(create_page()));
		m_open_pages.push(m_pages.size() - 1);
	}

	uint16_t page_idx = m_open_pages.top(); 

	assert(page_idx < m_pages.size());

	TilePage &page = m_pages[page_idx];

	uint16_t tex_idx = page.free_list.back();

	assert(tex_idx < TILE_PAGE_SIZE);

	page.free_list.pop_back();

	if (page.free_list.empty()) {
		m_open_pages.pop();
	}

	return TileTexIndex{
		.page = page_idx,
		.tex = tex_idx,
	};
}

void TileTexCache::deallocate(TileTexIndex idx)
{
	TilePage &page = m_pages[idx.page];

	if (page.free_list.empty()) {
		m_open_pages.push(idx.page);
	}

	page.free_list.push_back(idx.tex);
}

void TileTexCache::reserve(uint32_t count)
{
	assert(count <= MAX_TILES);

	size_t curr = m_pages.size() * TILE_PAGE_SIZE;

	if (curr >= count)
		return;

	size_t req = (count ? count - 1 : 0)/TILE_PAGE_SIZE + 1;
	size_t diff = req - m_pages.size();

	m_pages.reserve(req);

	for (size_t i = 0; i < diff; ++i) {
		m_pages.emplace_back(std::move(create_page()));
		m_open_pages.push(m_pages.size() - 1);
	}
}

TileTexIndex TileTexCache::evict_one()
{
	assert(!m_lru.empty());
	
	std::pair<TileCode, TileTexIndex> ent = m_lru.back();
	m_lru.pop_back();
	m_map.erase(ent.first);

	return ent.second;
}

void TileTexCache::insert(TileCode code, TileTexIndex idx)
{
	assert(m_map.find(code) == m_map.end());

	m_lru.push_front({code,idx});
	m_map[code] = m_lru.begin();
}

TileTexCache::TileTexCache()
{

}

void TileTexCache::get_textures(
	const std::span<TileCode> tiles, 
	std::vector<TileTexIndex>& textures,
	std::vector<std::pair<TileCode,TileTexIndex>>& new_tiles
)
{
	size_t tile_count = std::min(tiles.size(),(size_t)MAX_TILES);

	reserve(tile_count);

	for (size_t i = 0; i < tile_count; ++i) {
		TileCode code = tiles[i];
		auto it = m_map.find(code);

		const bool found = it != m_map.end();

		TileTexIndex idx {};

		if (found) {
			lru_list_t::iterator ent = it->second;
			m_lru.splice(m_lru.begin(), m_lru, ent);
			idx = ent->second;

		} else {
			idx = (m_map.size() >= MAX_TILES) ? 
				evict_one() : allocate();
			insert(code, idx);
		}

		textures.push_back(idx);
		if (!found) new_tiles.push_back({code, idx});
	}

	log_info("TileCache::get_textures : loaded %ld new tile textures",new_tiles.size());
}

void TileTexCache::bind_texture_arrays(uint32_t base) const 
{
	assert(m_pages.size() <= MAX_TILE_PAGES);

	for (size_t i = 0; i < m_pages.size(); ++i) {
		glActiveTexture(GL_TEXTURE0 + base + i);
		glBindTexture(GL_TEXTURE_2D_ARRAY, m_pages[i].tex_array);	
	}
}
