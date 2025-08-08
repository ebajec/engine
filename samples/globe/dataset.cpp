#include "dataset.h"
#include "tiling.h"
#include "utils/log.h"

#include <imgui.h>

static void test_loader_fn(TileCode code, void *dst, void *usr);
static float test_elev_fn(glm::dvec2 uv, uint8_t f);

//------------------------------------------------------------------------------
// TileDataSource

TileDataSource 
*TileDataSource::create(TileLoaderFunc loader, void* usr)
{
	TileDataSource *source = new TileDataSource;

	source->m_loader = loader ? loader : test_loader_fn;
	source->m_usr = usr;

	return source;
}

void TileDataSource::load(TileCode code, void* dst) const
{
	m_loader(code,dst,m_usr);
}

TileCode TileDataSource::find(TileCode code) const
{
	auto end = m_data.end();
	auto it = m_data.find(code);

	while (code.zoom > m_debug_zoom && it == end) {
		code.idx >>= 2;
		--code.zoom;

		it = m_data.find(code);
	} 

	return (it == end) ? code : it->first;
}

float TileDataSource::sample_elevation_at(glm::dvec2 uv, uint8_t f) const
{
	return test_elev_fn(uv,f);
}

float TileDataSource::sample_elevation_at(glm::dvec3 p) const
{
	glm::dvec2 uv;
	uint8_t f;
	globe_to_cube(p, &uv, &f);

	return test_elev_fn(uv,f);
}
//------------------------------------------------------------------------------
// TileDataCache

TileDataCache 
*TileDataCache::create(size_t tile_size)
{
	TileDataCache *cache = new TileDataCache{};

	size_t size = cache->m_block_cap*tile_size;
	
	cache->m_block_size = tile_size;
	cache->m_blocks.reset(new uint8_t[size]);
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

std::vector<TileCode> TileDataCache::load(
	const TileDataSource& source, const std::span<TileCode> tiles)
{
	std::vector<TileCode> loaded (tiles.size());


	ImGui::SliderInt("Max zoom", const_cast<int*>(&source.m_debug_zoom), 0, 24);

	for (size_t i = 0; i < tiles.size(); ++i) {
		TileCode code = tiles[i];
		loaded[i] = source.find(code);
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
		source.load(pair.first, pair.second);
	}

	return loaded;
}

float test_elev_fn(glm::dvec2 uv, uint8_t f)
{
	glm::dvec3 p = cube_to_globe(f, uv);

	float c = 15;
	float g = sin(c*p.x)*cos(c*p.y)*
			  sin(c*p.z)*cos(c*p.z);

	g *= 0.1;

	return g;
}

void test_loader_fn(TileCode code, void *dst, void *usr)
{
	float *data = static_cast<float*>(dst);

	aabb2_t rect = morton_u64_to_rect_f64(code.idx, code.zoom);

	float d = 1.0/(float)(TILE_WIDTH - 1);

	glm::vec2 uv = glm::vec2(0);
	size_t idx = 0;
	for (size_t i = 0; i < TILE_WIDTH; ++i) {
		for (size_t j = 0; j < TILE_WIDTH; ++j) {
			glm::vec2 f = glm::vec2(rect.min) + 
				uv * glm::vec2((rect.max - rect.min));

			float g = test_elev_fn(f, code.face);
			data[idx++] = g;
			uv.x += d;
		}
		uv.x = 0;
		uv.y += d;
	}

	return;
}



