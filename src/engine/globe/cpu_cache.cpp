#include "thread_pool.h"

#include "globe/tiling.h"
#include "globe/cpu_cache.h"

#include "utils/log.h"

#include <imgui.h>

#include <atomic>

static int MAX_TILES_IN_FLIGHT = 9;
static std::atomic_int g_tiles_in_flight = 0;

static void test_loader_fn(TileCode code, void *dst, void *usr,
						   const std::atomic<TileCPULoadState> *p_state);
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

TileCPUCache 
*TileCPUCache::create(size_t tile_size, size_t capacity)
{
	TileCPUCache *cache = new TileCPUCache{};
	cache->m_tile_size = tile_size;
	cache->m_tile_cap = (std::max(capacity,(size_t)1) - 1)/tile_size + 1;
	return cache;
}

TileCPUCache::entry_t *TileCPUCache::get_entry(TileCPUIndex idx) const
{
	return &m_pages[idx.page]->entries[idx.ent];
}

uint8_t *TileCPUCache::get_block(TileCPUIndex idx) const
{
	return &m_pages[idx.page]->mem[idx.ent*m_tile_size];
}

TileCode TileCPUCache::find_best(TileCode code)
{
	auto end = m_map.end();
	auto it = m_map.find(code);

	TileCPULoadStatus status = TILE_CPU_STATE_EMPTY;

	while (code.zoom > 0 && status != TILE_CPU_STATE_READY) {
		code.idx >>= 2;
		--code.zoom;

		it = m_map.find(code);

		if (it != end) {
			status = get_entry(*(it->second))->state.load().status;
		}
	} 

	if (status == TILE_CPU_STATE_READY) {
		lru_list_t::iterator l_it = it->second;
		m_lru.splice(m_lru.begin(), m_lru, l_it);
		return it->first;
	}

	//log_info("No loaded parent found for tile %d",in);

	return TILE_CODE_NONE;
}

static TileCPUCache::page_t *create_page(size_t block_size)
{
	TileCPUCache::page_t *page = new TileCPUCache::page_t{};
	page->free_list.resize(TILE_CPU_PAGE_SIZE);

	for (size_t i = 0; i < TILE_CPU_PAGE_SIZE; ++i) {
		page->free_list[i] = static_cast<uint32_t>(TILE_CPU_PAGE_SIZE - i - 1);
	}

	page->mem.reset(new uint8_t[block_size*TILE_CPU_PAGE_SIZE]);

	return page;
}

TileCPUIndex TileCPUCache::allocate()
{
	if (m_open_pages.empty()) {
		m_pages.emplace_back(std::move(create_page(m_tile_size)));
		m_open_pages.push(static_cast<uint16_t>(m_pages.size() - 1));
	}

	uint16_t page = m_open_pages.top(); 

	assert(page < m_pages.size());

	page_t *p_page = m_pages[page].get();

	uint32_t ent = p_page->free_list.back();

	assert(ent < TILE_CPU_PAGE_SIZE);

	p_page->free_list.pop_back();

	if (p_page->free_list.empty()) {
		m_open_pages.pop();
	}

	return TileCPUIndex{
		.page = page,
		.ent = ent,
	};

}

TileCPUIndex TileCPUCache::evict_one()
{
	TileCPUIndex idx = m_lru.back(); 
	entry_t *ent = get_entry(idx);

	TileCPULoadState state = ent->state.load();
	TileCPULoadState desired = {.status = TILE_CPU_STATE_EMPTY, .refs = 0};
	do {
		if (state.refs > 0) {
			log_info("Could not evict tile %d from CPU cache; in use",ent->code);
			return TILE_CPU_INDEX_NONE;
		}

		if (state.status == TILE_CPU_STATE_CANCELLED) {
			return TILE_CPU_INDEX_NONE;
		}

		if (state.status == TILE_CPU_STATE_LOADING || 
			state.status == TILE_DATA_STATE_QUEUED)
			desired = {.status = TILE_CPU_STATE_CANCELLED, .refs = state.refs};
		else 
			desired = {.status = TILE_CPU_STATE_EMPTY, .refs = 0};
	} while (!ent->state.compare_exchange_weak(state, desired));

	if (desired.status == TILE_CPU_STATE_CANCELLED) {
		log_info("Cancelled load for tile %d",ent->code.u64);
		return TILE_CPU_INDEX_NONE;
	}

	if (m_map.find(ent->code) == m_map.end()) {
		log_error("Failed to evict entry at %d; not contained in table!",ent->code.u64);
		return TILE_CPU_INDEX_NONE;
	}

	//log_info("Evicted tile %d from CPU cache",ent->code);

	m_map.erase(ent->code);	
	m_lru.pop_back();

	return idx;
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

	TileCPUIndex idx = *(it->second);
	entry_t *ent = get_entry(idx);

	if (p_state) *p_state = &ent->state;
	if (size) *size = m_tile_size;

	return get_block(idx);
}

std::optional<TileDataRef> TileCPUCache::acquire_block(TileCode code) const
{
	auto it = m_map.find(code);

	if (it == m_map.end()) {
		log_error("acquire_block: Failed to find tile with code %ld (face=%d,zoom=%d,idx=%d)",
			code.u64, code.face,code.zoom,code.idx);
		return std::nullopt;
	}

	TileCPUIndex idx = *it->second;
	entry_t *ent = get_entry(idx);

	TileCPULoadState state = ent->state.load();
	do {
		if (state.status != TILE_CPU_STATE_READY)
			return std::nullopt;
	} while (!ent->state.compare_exchange_weak(state, {
		.status = TILE_CPU_STATE_READY, 
		.refs = state.refs + 1
	}));

	//log_info("Acquired tile %d from CPU cache");

	TileDataRef ref = {
		.data = get_block(idx),
		.size = m_tile_size,
		.p_state = &ent->state
	};

	return ref;
}
void TileCPUCache::release_block(TileDataRef ref) const
{
	TileCPULoadState state = ref.p_state->load();
	TileCPULoadState desired;
	do {
		desired = {.status = TILE_CPU_STATE_READY, .refs = state.refs - 1};
	} while (!ref.p_state->compare_exchange_weak(state, desired));

	//log_info("Released tile %d from CPU cache");
}

static void load_thread_fn(
	const TileDataSource *source, 
	TileCode code,
	std::atomic<TileCPULoadState> *p_state, 
	uint8_t* dst)
{
	TileCPULoadState state;
	do {
		state = p_state->load();
		if (state.status == TILE_CPU_STATE_CANCELLED) {
			p_state->store({.status = TILE_CPU_STATE_EMPTY, .refs = state.refs});
			log_info("load cancelled successfully (tile %d)",code);
			return;
		}
	} while (!p_state->compare_exchange_weak(state, {
		.status = TILE_CPU_STATE_LOADING, .refs = state.refs
	}));

	source->m_loader(code,dst,source->m_usr,p_state);

	TileCPULoadState desired;
	do {
		state = p_state->load();
		if (state.status == TILE_CPU_STATE_CANCELLED)
			desired = {.status = TILE_CPU_STATE_EMPTY, .refs = state.refs};
		else 
			desired = {.status = TILE_CPU_STATE_READY, .refs = state.refs};
	} while(!p_state->compare_exchange_weak(state, desired));

	if (desired.status == TILE_CPU_STATE_EMPTY) 
		log_info("load cancelled successfully (tile %d)",code);

	//log_info("Tile %d ready",ent->code);
}

std::vector<TileCode> TileCPUCache::update(
	const TileDataSource& source, const std::span<TileCode> tiles)
{
	std::vector<TileCode> available (tiles.size());

	ImGui::SliderInt("Max zoom", const_cast<int*>(&source.m_debug_zoom), 0, 24);

	for (size_t i = 0; i < tiles.size(); ++i) {
		TileCode code = tiles[i];
		available[i] = source.find(code);
	}

	std::vector<TileCode> loaded (tiles.size(),TILE_CODE_NONE);
	std::vector<std::pair<TileCPUIndex,entry_t*>> loads;

	size_t count = std::min(available.size(),m_tile_cap);

	if (count < available.size()) {
		log_warn("Number of requested tiles exceeds cache size");
	}

	for (size_t i = 0; i < count; ++i) {
		TileCode ideal = available[i];
		TileCode code = TILE_CODE_NONE; 

		auto it = m_map.find(ideal);
		if (it != m_map.end()) {
			lru_list_t::iterator l_ent = it->second;
			m_lru.splice(m_lru.begin(), m_lru, l_ent);

			TileCPUIndex idx = *it->second;
			entry_t *ent = get_entry(idx);

			TileCPULoadState state = ent->state.load(); 

			if (state.status == TILE_CPU_STATE_EMPTY) {
				if (state.refs > 0) 
					log_error("Empty tile has %d references",state.refs);
				loads.push_back({idx,ent});
				goto end_loop;
			}

			if (state.status == TILE_CPU_STATE_READY)
				code = ideal;
		} else {
			TileCPUIndex idx = m_map.size() >= m_tile_cap ?
	  			evict_one() : allocate();

			if (!idx.is_valid()) 
				goto end_loop;

			m_lru.push_front(idx);
			m_map[ideal] = m_lru.begin();

			entry_t *ent = get_entry(idx);
			ent->code = ideal,
			ent->state = {.status = TILE_CPU_STATE_EMPTY, .refs = 0},

			loads.push_back({idx,ent});
		}

	end_loop:
		if (code == TILE_CODE_NONE)
			code = find_best(ideal);
		loaded[i] = code;
	}

	for (auto [idx, ent] : loads) {
		uint8_t *dst = get_block(idx);

		if (g_tiles_in_flight < MAX_TILES_IN_FLIGHT) {
			//log_info("Loading tile %d",ent->code);

			ent->state.store({.status = TILE_DATA_STATE_QUEUED, .refs = 0});
			g_schedule_background([=](){
				++g_tiles_in_flight;
				load_thread_fn(&source, ent->code, &ent->state, dst);
				--g_tiles_in_flight;
			});
		}
	}

	return loaded;
}

static constexpr size_t coeff_count = 5;
static float coeffs[coeff_count] = {};

static void init_coeffs()
{
	for (size_t i = 0; i < coeff_count; ++i) {
		coeffs[i] = (1.0f - (float)rand())/(float)RAND_MAX; 
	}
}

static float test_elev_fn2(glm::dvec2 uv, uint8_t f, uint8_t zoom)
{
	glm::dvec3 p = cube_to_globe(f, uv);

	static int init = 0;

	if (!init) {
		++init;
		init_coeffs();
	}

	float g = 0;
	for (size_t i = 0; i < coeff_count; ++i) {
		double c = 4*((double)i + 1);
		g += (coeffs[i]/(float)c)*(float)(sin(c*p.x)*cos(c*p.y)*sin(c*p.z)*cos(c*p.z*p.x));
	}

	g *= 0.4f;

	return g;
}

float test_elev_fn(glm::dvec2 uv, uint8_t f)
{
	return test_elev_fn2(uv, f,coeff_count);
}

void test_loader_fn(TileCode code, void *dst, void *usr, 
					const std::atomic<TileCPULoadState> *p_state)
{
	float *data = static_cast<float*>(dst);

	aabb2_t rect = morton_u64_to_rect_f64(code.idx, code.zoom);

	float d = 1.0f/(float)(TILE_WIDTH - 1);

	glm::vec2 uv = glm::vec2(0);
	size_t idx = 0;
	for (size_t i = 0; i < TILE_WIDTH; ++i) {
		if (p_state->load().status == TILE_CPU_STATE_CANCELLED)
			return;

		for (size_t j = 0; j < TILE_WIDTH; ++j) {
			glm::vec2 f = glm::vec2(rect.min) + 
				uv * glm::vec2((rect.max - rect.min));

			float g = test_elev_fn2(f, code.face, code.zoom);
			data[idx++] = g;
			uv.x += d;
		}
		uv.x = 0;
		uv.y += d;
	}

	return;
}



