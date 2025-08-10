#include "dataset.h"
#include "tiling.h"
#include "utils/log.h"
#include "thread_pool.h"

#include <imgui.h>

#include <atomic>

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

TileCode TileCPUCache::find_best(TileCode code) const
{
	auto end = m_map.end();
	auto it = m_map.find(code);

	TileCPULoadState state = TILE_DATA_STATE_EMPTY;

	while (code.zoom > 0 && state != TILE_DATA_STATE_READY) {
		code.idx >>= 2;
		--code.zoom;

		it = m_map.find(code);

		if (it != end) {
			log_info("Found parent tile %d",code);
			state = it->second->state;
		}
	} 

	return (state == TILE_DATA_STATE_READY) ? it->first : TILE_CODE_NONE;
}

TileCPUCache 
*TileCPUCache::create(size_t tile_size)
{
	TileCPUCache *cache = new TileCPUCache{};

	size_t size = cache->m_block_cap*tile_size;
	
	cache->m_block_size = tile_size;
	cache->m_blocks.reset(new uint8_t[size]);
	return cache;
}

size_t TileCPUCache::evict()
{
	entry_t &ent = m_lru.back();

	TileCPULoadState state;
	do {
		state = ent.state.load();
		if (state == TILE_DATA_STATE_LOADING || 
			state == TILE_DATA_STATE_CANCELLED || ent.refs > 0) {
			return UINT64_MAX;
		}
	} while (!ent.state.compare_exchange_weak(state, TILE_DATA_STATE_EMPTY));

	if (m_map.find(ent.code) == m_map.end()) {
		log_error("Failed to evict entry at %d; not contained in table!",ent.code.u64);
		return UINT64_MAX;
	}

	size_t offset = ent.block_offset;

	m_map.erase(ent.code);	
	m_lru.pop_back();

	return offset;
}

const uint8_t *TileCPUCache::acquire_block(TileCode code, size_t *size,
									   std::atomic<TileCPULoadState> **p_state) const
{
	auto it = m_map.find(code);

	if (it == m_map.end()) {
		log_error("TileDataCache::get_block : Failed to find tile with id %ld",
			code.u64);
		return nullptr;
	}
	if (p_state) *p_state = &it->second->state;
	if (size) *size = m_block_size;

	return &m_blocks[it->second->block_offset];
}

std::optional<TileDataRef> TileCPUCache::acquire_block(TileCode code) const
{
	auto it = m_map.find(code);

	if (it == m_map.end()) {
		log_error("acquire_block: Failed to find tile with code %ld (face=%d,zoom=%d,idx=%d)",
			code.u64, code.face,code.zoom,code.idx);
		return std::nullopt;
	}

	if (it->second->state != TILE_DATA_STATE_READY) 
		return std::nullopt;

	TileDataRef ref = {
		.data = &m_blocks[it->second->block_offset],
		.size = m_block_size,
		.p_state = &it->second->state,
		.p_refs = &it->second->refs,
	};

	ref.p_refs->fetch_add(1);

	return ref;
}
void TileCPUCache::release_block(TileDataRef ref) const
{
	ref.p_refs->fetch_sub(1);
}

void load_thread_fn(const TileDataSource *source, TileCPUCache::entry_t *ent, uint8_t* dst)
{
	TileCPULoadState state;
	do {
		state = ent->state.load();
		if (state != TILE_DATA_STATE_QUEUED)
			return;
	} while (!ent->state.compare_exchange_weak(state, TILE_DATA_STATE_LOADING));

	source->load(ent->code, dst);

	ent->state.store(TILE_DATA_STATE_READY);
}

std::vector<TileCode> TileCPUCache::load(
	const TileDataSource& source, const std::span<TileCode> tiles)
{
	std::vector<TileCode> available (tiles.size());

	ImGui::SliderInt("Max zoom", const_cast<int*>(&source.m_debug_zoom), 0, 24);

	for (size_t i = 0; i < tiles.size(); ++i) {
		TileCode code = tiles[i];
		available[i] = source.find(code);
	}

	std::vector<TileCode> loaded (tiles.size());

	std::vector<entry_t*> loads;
	for (size_t i = 0; i < available.size(); ++i) {
		TileCode ideal = available[i];
		TileCode code = TILE_CODE_NONE; 

		auto it = m_map.find(ideal);
		if (it != m_map.end()) {
			lru_list_t::iterator ent = it->second;
			m_lru.splice(m_lru.begin(), m_lru, ent);

			TileCPULoadState state = ent->state.load(); 
			if (state != TILE_DATA_STATE_READY) {
				continue;
			}
			code = ideal;
		} else {
			size_t offset = m_block_idx < m_block_cap ? 
				(m_block_idx++)*m_block_size : evict();	

			entry_t &ent = m_lru.emplace_front();
			ent.code = ideal,
			ent.block_offset = offset,
			ent.state = TILE_DATA_STATE_EMPTY,
			ent.refs = 0,

			m_map[ideal] = m_lru.begin();

			loads.push_back(&m_lru.front());
		}

		if (code == TILE_CODE_NONE) {
			code = find_best(ideal);
		}

		loaded[i] = code;
	}


	for (entry_t *ent : loads) {
		uint8_t *dst = &m_blocks[ent->block_offset];

		ent->state.store(TILE_DATA_STATE_QUEUED);
		g_schedule_task([=](){
			load_thread_fn(&source, ent, dst);
		});
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



