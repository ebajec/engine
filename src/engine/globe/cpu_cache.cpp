#include "thread_pool.h"

#include "globe/tiling.h"
#include "globe/cpu_cache.h"

#include "utils/log.h"

#include <imgui.h>

#include <atomic>
#include <thread>

static int MAX_TILES_IN_FLIGHT = std::thread::hardware_concurrency()/2;
static std::atomic_int g_tiles_in_flight = 0;

static void test_loader_fn(TileCode code, void *dst, void *usr,
						   const ct_atomic_state *p_state);
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
// ct impl

static int ct_create_page(ct_table *ct, ct_page *page)
{
	size_t size = ct->page_size;
	page->free_list.resize(size);

	for (size_t i = 0; i < size; ++i) {
		page->free_list[i] = static_cast<uint32_t>(size - i - 1);
	}

	page->entries = (ct_entry*)calloc(size, sizeof(ct_entry));

	return ct->page_create(ct->usr, &page->handle);
}

static ct_index ct_allocate(ct_table *ct)
{
	if (ct->open_pages.empty()) {
		ct->pages.emplace_back();

		if (ct_create_page(ct, &ct->pages.back()) < 0)
			throw std::invalid_argument("Failed to create cache page");

		ct->open_pages.push(static_cast<uint16_t>(ct->pages.size() - 1));
	}
	
	uint16_t page = ct->open_pages.top(); 

	assert(page < ct->pages.size());

	ct_page *p_page = &ct->pages[page];

	uint32_t ent = p_page->free_list.back();

	assert(ent < ct->page_size);

	p_page->free_list.pop_back();

	if (p_page->free_list.empty()) {
		ct->open_pages.pop();
	}

	return ct_index{
		.page = page,
		.ent = ent,
	};
}

static ct_index ct_evict_one(ct_table *ct)
{
	ct_index idx = ct->lru.back(); 
	ct_entry *ent = &ct->pages[idx.page].entries[idx.ent];

	uint64_t state_packed = ent->state.load();
	ct_state desired = {
		.status = CT_STATUS_EMPTY, 
		.flags = 0,
		.gen = 0,
		.refs = 0
	};

	do {
		ct_state state = ct_state_unpack(state_packed);
		if (state.refs > 0) {
			log_info("Could not evict entry %d from cache; in use",ent->key);
			return CT_INDEX_NONE;
		}

		if (state.status == CT_STATUS_CANCELLED) {
			return CT_INDEX_NONE;
		}

		if (state.status == CT_STATUS_LOADING || 
			state.status == CT_STATUS_QUEUED)
			desired = {
				.status = CT_STATUS_CANCELLED, 
				.flags = 0,
				.gen = 0,
				.refs = state.refs
			};
		else 
			desired = {
				.status = CT_STATUS_EMPTY,
				.flags = 0,
				.gen = 0,
				.refs = 0
			};
	} while (!ent->state.compare_exchange_weak(state_packed, ct_state_pack(desired)));

	if (desired.status == CT_STATUS_CANCELLED) {
		log_info("Cancelled load for tile %d",ent->key);
		return CT_INDEX_NONE;
	}

	if (ct->map.find(ent->key) == ct->map.end()) {
		log_error("Failed to evict entry at %d; not contained in table!",ent->key);
		return CT_INDEX_NONE;
	}

	//log_info("Evicted tile %d from CPU cache",ent->code);

	ct->map.erase(ent->key);	
	ct->lru.pop_back();

	return idx;
}

ct_load_result ct_table_load(ct_table *ct, uint64_t key)
{
	ct_load_result res {};

	auto it = ct->map.find(key);
	if (it != ct->map.end()) {
		ct_lru_list::iterator l_ent = it->second;
		ct->lru.splice(ct->lru.begin(), ct->lru, l_ent);

		ct_index idx = *it->second;
		ct_entry *ent = &ct->pages[idx.page].entries[idx.ent];
		ct_state state = ct_state_unpack(ent->state.load()); 

		if (state.status == CT_STATUS_EMPTY) {
			if (state.refs > 0) 
				log_error("Empty tile has %d references",state.refs);

			res.idx = idx;
			res.p_ent = ent;
			res.needs_load = true;

		} else if (state.status == CT_STATUS_EMPTY) {
			res.is_ready = true;
		}
	} else {
#if 1 
		ct_index idx = ct->map.size() >= ct->capacity ?
	  		ct_evict_one(ct) : ct_allocate(ct);

		if (!idx.is_valid()) 
			return res;

		ct->lru.push_front(idx);
		ct->map[key] = ct->lru.begin();

		ct_entry *ent = &ct->pages[idx.page].entries[idx.ent];
		ent->key = key,
		ent->state = ct_state_pack({
			.status = CT_STATUS_EMPTY, 
			.flags = 0,
			.gen = 0,
			.refs = 0
		}),

		res.idx = idx;
		res.p_ent = ent;
		res.needs_load = true;
#endif
	}

	return res;
}

int ct_table_create(ct_table **p_ct, ct_table_create_info const *ci)
{
	ct_table *ct = new ct_table {};

	ct->page_create = ci->page_create;
	ct->page_size = ci->page_size;
	ct->capacity = ci->capacity;
	ct->usr = ci->usr;

	*p_ct = ct;

	return 0;
}

void ct_table_destroy(ct_table *ct)
{
	delete ct;
}

//------------------------------------------------------------------------------
// TileDataCache

static int create_cpu_tile_page(void *usr, ct_page_handle_t *p_handle)
{
	const TileCPUCache *cache = static_cast<TileCPUCache*>(usr);

	uint8_t * mem = new uint8_t[cache->m_tile_size*cache->m_ct->page_size];
	uintptr_t ptr = reinterpret_cast<uintptr_t>(mem);

	*p_handle = static_cast<uint64_t>(ptr);

	return 0;
}

TileCPUCache 
*TileCPUCache::create(size_t tile_size, size_t capacity)
{
	TileCPUCache *cache = new TileCPUCache{};
	cache->m_tile_size = tile_size;
	cache->m_tile_cap = (std::max(capacity,(size_t)1) - 1)/tile_size + 1;

	ct_table *ct = nullptr;
	ct_table_create_info ci = {
		.capacity = cache->m_tile_cap,
		.page_size = TILE_CPU_PAGE_SIZE,
		.usr = cache,
		.page_create = &create_cpu_tile_page,
	};

	if (ct_table_create(&ct, &ci) < 0)
		goto cleanup;

	cache->m_ct.reset(ct);

	return cache;
cleanup:
	delete cache;
	return nullptr;
}

#if 0
TileCPUCache::entry_t *TileCPUCache::get_entry(TileCPUIndex idx) const
{
	return &m_pages[idx.page]->entries[idx.ent];
}
#endif

static ct_entry * ct_entry_get(ct_table *ct, ct_index idx)
{
	return &ct->pages[idx.page].entries[idx.ent];
}

TileCode TileCPUCache::find_best(TileCode code)
{
	auto end = m_ct->map.end();
	auto it = m_ct->map.find(code.u64);

	ct_status status = CT_STATUS_EMPTY;

	while (code.zoom > 0 && status != CT_STATUS_READY) {
		code.idx >>= 2;
		--code.zoom;

		it = m_ct->map.find(code.u64);

		if (it != end) {
			ct_index idx = *it->second;
			ct_entry *ent = ct_entry_get(m_ct.get(), idx);
			status = ct_state_status(ent->state.load());
		}
	} 

	if (status == CT_STATUS_READY) {
		ct_lru_list::iterator l_it = it->second;
		m_ct->lru.splice(m_ct->lru.begin(), m_ct->lru, l_it);
		return TileCode{.u64 = it->first};
	}

	//log_info("No loaded parent found for tile %d",in);

	return TILE_CODE_NONE;
}

uint8_t *TileCPUCache::get_block(ct_index idx) const
{
	ct_page_handle_t pg_handle = m_ct->pages[idx.page].handle; 

	uint8_t *mem = reinterpret_cast<uint8_t*>(pg_handle);

	return &mem[idx.ent*m_tile_size];
}

#if 0
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
	TileCPULoadState desired = {.status = CT_STATUS_EMPTY, .refs = 0};
	do {
		if (state.refs > 0) {
			log_info("Could not evict tile %d from CPU cache; in use",ent->code);
			return TILE_CPU_INDEX_NONE;
		}

		if (state.status == CT_STATUS_CANCELLED) {
			return TILE_CPU_INDEX_NONE;
		}

		if (state.status == CT_STATUS_LOADING || 
			state.status == CT_STATUS_QUEUED)
			desired = {.status = CT_STATUS_CANCELLED, .refs = state.refs};
		else 
			desired = {.status = CT_STATUS_EMPTY, .refs = 0};
	} while (!ent->state.compare_exchange_weak(state, desired));

	if (desired.status == CT_STATUS_CANCELLED) {
		log_info("Cancelled load for tile %d",ent->code.u64);
		return TILE_CPU_INDEX_NONE;
	}

	if (m_map.find(ent->code.u64) == m_map.end()) {
		log_error("Failed to evict entry at %d; not contained in table!",ent->code.u64);
		return TILE_CPU_INDEX_NONE;
	}

	//log_info("Evicted tile %d from CPU cache",ent->code);

	m_map.erase(ent->code.u64);	
	m_lru.pop_back();

	return idx;
}

#endif

const uint8_t *TileCPUCache::acquire_block(TileCode code, size_t *size,
									   std::atomic_uint64_t **p_state) const
{
	auto it = m_ct->map.find(code.u64);

	if (it == m_ct->map.end()) {
		log_error("TileDataCache::get_block : Failed to find tile with id %ld",
			code.u64);
		return nullptr;
	}

	ct_index idx = *(it->second);
	ct_entry *ent = ct_entry_get(m_ct.get(),idx);

	if (p_state) *p_state = &ent->state;
	if (size) *size = m_tile_size;

	return get_block(idx);
}

std::optional<TileDataRef> TileCPUCache::acquire_block(TileCode code) const
{
	auto it = m_ct->map.find(code.u64);

	if (it == m_ct->map.end()) {
		log_error("acquire_block: Failed to find tile with code %ld (face=%d,zoom=%d,idx=%d)",
			code.u64, code.face,code.zoom,code.idx);
		return std::nullopt;
	}

	ct_index idx = *it->second;
	ct_entry *ent = ct_entry_get(m_ct.get(),idx);

	uint64_t state = ent->state.load();
	ct_state desired;
	do {
		if (ct_state_status(state) != CT_STATUS_READY)
			return std::nullopt;

		desired = ct_state_unpack(state); 
		desired.status = CT_STATUS_READY;
		++desired.refs;
	} while (!ent->state.compare_exchange_weak(state, ct_state_pack(desired)));

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
	uint64_t state = ref.p_state->load();
	uint64_t desired;
	do {
		desired = ct_state_pack({
			.status = CT_STATUS_READY, 
			.flags = 0,
			.gen = 0,
			.refs = ct_state_refs(state) - 1
		});
	} while (!ref.p_state->compare_exchange_weak(state, desired));

	//log_info("Released tile %d from CPU cache");
}

static void load_thread_fn(
	const TileDataSource *source, 
	TileCode code,
	ct_atomic_state *p_state, 
	uint8_t* dst)
{
	uint64_t state_pkd = p_state->load();
	ct_state desired;
	do {
		ct_state state = ct_state_unpack(state_pkd);
		desired = state;
		// If it was cancelled then we are the one stuff is waiting on to 
		// se the status back to empty
		if (state.status == CT_STATUS_CANCELLED) {
			desired.status = CT_STATUS_EMPTY;
			p_state->store(ct_state_pack(desired));
			log_info("load cancelled successfully (tile %d)",code);

			return;
		} else {
			desired.status = CT_STATUS_LOADING;
		}
	} while (!p_state->compare_exchange_weak(state_pkd, ct_state_pack(desired)));

	source->m_loader(code,dst,source->m_usr,p_state);

	state_pkd = p_state->load();
	do {
		ct_state state = ct_state_unpack(state_pkd);
		desired = state;
		if (state.status == CT_STATUS_CANCELLED)
			desired.status = CT_STATUS_EMPTY;
		else 
			desired.status = CT_STATUS_READY;
	} while(!p_state->compare_exchange_weak(state_pkd, ct_state_pack(desired)));

	if (desired.status == CT_STATUS_EMPTY) 
		log_info("load cancelled successfully (tile %d)",code);

	//log_info("Tile %d ready",ent->code);
}

#if 0
TileCPUCache::result_t TileCPUCache::load(uint64_t key)
{
	result_t res {};

	auto it = m_map.find(key);
	if (it != m_map.end()) {
		lru_list_t::iterator l_ent = it->second;
		m_lru.splice(m_lru.begin(), m_lru, l_ent);

		TileCPUIndex idx = *it->second;
		entry_t *ent = get_entry(idx);

		TileCPULoadState state = ent->state.load(); 

		if (state.status == CT_STATUS_EMPTY) {
			if (state.refs > 0) 
				log_error("Empty tile has %d references",state.refs);

			res.idx = idx;
			res.p_ent = ent;
			res.needs_load = true;

		} else if (state.status == CT_STATUS_READY) {
			res.is_ready = true;
		}
	} else {
		TileCPUIndex idx = m_map.size() >= m_tile_cap ?
	  		evict_one() : allocate();

		if (!idx.is_valid()) 
			return res;

		m_lru.push_front(idx);
		m_map[key] = m_lru.begin();

		entry_t *ent = get_entry(idx);
		ent->code.u64 = key,
		ent->state = {.status = CT_STATUS_EMPTY, .refs = 0},

		res.idx = idx;
		res.p_ent = ent;
		res.needs_load = true;
	}

	return res;
}
#endif

std::vector<TileCode> TileCPUCache::update(
	const TileDataSource& source, const std::span<TileCode> tiles)
{
	std::vector<TileCode> available (tiles.size());

	for (size_t i = 0; i < tiles.size(); ++i) {
		TileCode code = tiles[i];
		available[i] = source.find(code);
	}

	std::vector<TileCode> loaded (tiles.size(),TILE_CODE_NONE);
	std::vector<std::pair<ct_index,ct_entry*>> loads;

	size_t count = std::min(available.size(),m_tile_cap);

	if (count < available.size()) {
		log_warn("Number of requested tiles exceeds cache size");
	}

	for (size_t i = 0; i < count; ++i) {
		TileCode ideal = available[i];
		ct_load_result res = ct_table_load(m_ct.get(),ideal.u64);

		if (res.needs_load)
			loads.push_back({res.idx,res.p_ent});

		TileCode code = res.is_ready ? ideal : find_best(ideal); 

		loaded[i] = code;
	}

	for (auto [idx, ent] : loads) {
		uint8_t *dst = get_block(idx);

		if (g_tiles_in_flight < MAX_TILES_IN_FLIGHT) {
			//log_info("Loading tile %d",ent->code);

			ent->state.store(ct_state_pack({
				.status = CT_STATUS_QUEUED,
				.flags = 0,
				.gen = 0,
				.refs = 0
			}));
			g_schedule_background([=](){
				++g_tiles_in_flight;
				load_thread_fn(
					&source, 
					TileCode{.u64 = ent->key}, 
					&ent->state, 
					dst
				);
				--g_tiles_in_flight;
			});
		}
	}

	return loaded;
}

static constexpr size_t coeff_count = 5;
static double coeffs[coeff_count] = {};
static glm::dvec3 phases[coeff_count] = {};

static double urandf1()
{
	return (1.0 - (double)rand())/(double)RAND_MAX;
}

static void init_coeffs()
{
	for (size_t i = 0; i < coeff_count; ++i) {
		coeffs[i] = (1.0 - (double)rand())/(double)RAND_MAX; 
		phases[i] = glm::vec3(
			urandf1(),urandf1(),urandf1());
	}
}

static double test_elev_fn2(glm::dvec2 uv, uint8_t f, uint8_t zoom)
{
	glm::dvec3 p = cube_to_globe(f, uv);

	static int init = 0;

	if (!init) {
		++init;
		init_coeffs();
	}

	double g = 0;
	for (size_t i = 0; i < coeff_count; ++i) {
		double idx = (double)i + 1;
		double c = 1024*(idx);
		glm::dvec3 h = 512.0*phases[i]*TWOPI;
		g += (coeffs[i]/(double)idx)*(sin(c*p.x - h.x)*sin(c*p.y - h.y)*sin(c*p.z - h.z));
	}

	g *= 0.00138;

	return g;
}

float test_elev_fn(glm::dvec2 uv, uint8_t f)
{
	return (float)test_elev_fn2(uv, f,coeff_count);
}

void test_loader_fn(TileCode code, void *dst, void *usr, 
					const ct_atomic_state *p_state)
{
	float *data = static_cast<float*>(dst);

	aabb2_t rect = morton_u64_to_rect_f64(code.idx, code.zoom);

	float d = 1.0f/(float)(TILE_WIDTH - 1);

	glm::vec2 uv = glm::vec2(0);
	size_t idx = 0;
	for (size_t i = 0; i < TILE_WIDTH; ++i) {
		if (ct_state_status(p_state->load()) == CT_STATUS_CANCELLED)
			return;

		for (size_t j = 0; j < TILE_WIDTH; ++j) {
			glm::vec2 f = glm::mix(rect.min,rect.max,uv);

			double g = test_elev_fn2(f, code.face, code.zoom);
			data[idx++] = static_cast<float>(g);
			uv.x += d;
		}
		uv.x = 0;
		uv.y += d;
	}

	return;
}



