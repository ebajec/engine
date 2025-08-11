#include "tile_cache.h"
#include "utils/log.h"
#include "thread_pool.h"

#include <cstring>

std::unique_ptr<TilePage> create_page(GLuint format)
{
	std::unique_ptr<TilePage> page (new TilePage{});

	glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &page->tex_array);
	glTextureStorage3D(
		page->tex_array, 
		1, 
		format, 
		TILE_WIDTH, 
		TILE_WIDTH, 
		TILE_PAGE_SIZE
	);

	glTextureParameteri(page->tex_array, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(page->tex_array, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(page->tex_array, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(page->tex_array, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	page->free_list.resize(TILE_PAGE_SIZE);

	for (uint16_t i = 0; i < TILE_PAGE_SIZE; ++i) {
		page->free_list[i] = TILE_PAGE_SIZE - i - 1;
	}

	return page;
}

void destroy_page(TilePage &page)
{
	glDeleteTextures(1,&page.tex_array);
}

TileTexIndex TileGPUCache::allocate()
{
	if (m_open_pages.empty()) {
		m_pages.emplace_back(std::move(create_page(m_data_format)));
		m_open_pages.push(m_pages.size() - 1);
	}

	uint16_t page_idx = m_open_pages.top(); 

	assert(page_idx < m_pages.size());

	TilePage *page = m_pages[page_idx].get();

	uint16_t tex_idx = page->free_list.back();

	assert(tex_idx < TILE_PAGE_SIZE);

	page->free_list.pop_back();

	if (page->free_list.empty()) {
		m_open_pages.pop();
	}

	return TileTexIndex{
		.page = page_idx,
		.tex = tex_idx,
	};
}

void TileGPUCache::deallocate(TileTexIndex idx)
{
	TilePage *page = m_pages[idx.page].get();

	if (page->free_list.empty()) {
		m_open_pages.push(idx.page);
	}

	page->free_list.push_back(idx.tex);
}

void TileGPUCache::reserve(uint32_t count)
{
	assert(count <= MAX_TILES);

	size_t curr = m_pages.size() * TILE_PAGE_SIZE;

	if (curr >= count)
		return;

	size_t req = (count ? count - 1 : 0)/TILE_PAGE_SIZE + 1;
	size_t diff = req - m_pages.size();

	m_pages.reserve(req);

	for (size_t i = 0; i < diff; ++i) {
		m_pages.emplace_back(std::move(create_page(m_data_format)));
		m_open_pages.push(m_pages.size() - 1);
	}
}

TileTexIndex TileGPUCache::evict_one()
{
	assert(!m_lru.empty());
	
	std::pair<TileCode, TileTexIndex> ent = m_lru.back();

	TileTexIndex idx = ent.second;

	std::atomic<TileGPULoadState> *tex_state = &m_pages[idx.page]->states[idx.tex]; 

	TileGPULoadState state;
	do {
		state = tex_state->load(); 
		if (state == TILE_TEX_STATE_CANCELLED) 
			return TILE_INDEX_NONE;
		if (state == TILE_TEX_STATE_UPLOADING) {
			tex_state->store(TILE_TEX_STATE_CANCELLED);
			return TILE_INDEX_NONE;
		}
	} while(!tex_state->compare_exchange_weak(state, TILE_TEX_STATE_EMPTY));

	m_lru.pop_back();
	m_map.erase(ent.first);

	return ent.second;
}

void TileGPUCache::insert(TileCode code, TileTexIndex idx)
{
	assert(m_map.find(code) == m_map.end());

	m_lru.push_front({code,idx});
	m_map[code] = m_lru.begin();
}

void TileGPUCache::update(
	const TileCPUCache *cpu_cache,
	const std::span<TileCode> tiles, 
	std::vector<TileTexIndex>& textures,
	std::vector<TileTexUpload>& new_tiles
)
{
	size_t tile_count = std::min(tiles.size(),(size_t)MAX_TILES);

	reserve(tile_count);

	for (size_t i = 0; i < tile_count; ++i) {
		TileCode code = tiles[i];

		if (code == TILE_CODE_NONE) {
			textures.push_back(TILE_INDEX_NONE);
			continue;
		}

		auto it = m_map.find(code);

		const bool found = it != m_map.end();

		TileTexIndex idx = TILE_INDEX_NONE;

		if (found) {
			lru_list_t::iterator ent = it->second;
			m_lru.splice(m_lru.begin(), m_lru, ent);
			idx = ent->second;

		} else {
			idx = (m_map.size() >= MAX_TILES) ? 
				evict_one() : allocate();

			if (idx.is_valid()) {
				insert(code, idx);

				new_tiles.push_back(TileTexUpload{
					.code = code, 
					.idx = idx
				});
			}
		}

		textures.push_back(idx);
	}

	asynchronous_upload(cpu_cache, new_tiles);
}

struct UploadContext
{
	GLuint pbo;
	const TileCPUCache* data_cache;
	void* mapped;
	std::atomic_int refs; 
};

struct TileTexUploadData
{
	std::atomic<TileGPULoadState> *p_state;
	size_t offset;
	TileCode code;
	TileTexIndex idx;
	TileDataRef data_ref;
};

void tile_upload_fn(
	UploadContext *ctx,
	TileTexUploadData data
)
{
	TileDataRef ref = data.data_ref;
	// Should always be valid if it got this far.
	assert(ref.data && ref.p_state && ref.p_refs);

	uint8_t* dst = reinterpret_cast<uint8_t*>(ctx->mapped) + data.offset; 

	TileGPULoadState gpu_state;
	do {
		gpu_state = data.p_state->load();
		if (gpu_state == TILE_TEX_STATE_CANCELLED) {
			log_info("tile_upload_fn : Cancelled upload");
			goto cleanup;
		}
		if (gpu_state != TILE_TEX_STATE_QUEUED) {
			log_error("tile_upload_fn : Invalid tile state");
			goto cleanup;
		}
	} while (!data.p_state->compare_exchange_weak(gpu_state, TILE_TEX_STATE_UPLOADING));

	memcpy(dst,ref.data,ref.size);

cleanup:
	ctx->data_cache->release_block(ref);
	--ctx->refs;
	return;
}

void TileGPUCache::asynchronous_upload(const TileCPUCache *data_cache,
	std::span<TileTexUpload> uploads)
{
	if (!uploads.size())
		return;

	std::unique_ptr<UploadContext> ctx (new UploadContext{});
	glCreateBuffers(1,&ctx->pbo);
	ctx->refs = 0;
	ctx->data_cache = data_cache;
	
	GLsizeiptr total_size = uploads.size()*m_tile_size_bytes;

	glNamedBufferStorage(
		ctx->pbo,
		total_size,
		nullptr,
		GL_MAP_WRITE_BIT | 
		GL_MAP_PERSISTENT_BIT | 
		GL_MAP_COHERENT_BIT | 
		GL_DYNAMIC_STORAGE_BIT
	);

	ctx->mapped = glMapNamedBufferRange(
		ctx->pbo, 
		0, 
		total_size, 
		GL_MAP_WRITE_BIT | 
		GL_MAP_PERSISTENT_BIT | 
		GL_MAP_COHERENT_BIT
	);

	std::vector<TileTexUploadData> upload_data (uploads.size());

	UploadContext *p_ctx = ctx.get();
	for (size_t i = 0; i < uploads.size(); ++i) {
		TileTexUpload upload = uploads[i];

		TileTexIndex idx = upload.idx;

		if (!idx.is_valid()) {
			upload_data[i].idx = TILE_INDEX_NONE;
			continue;
		}

		std::optional<TileDataRef> ref = data_cache->acquire_block(upload.code); 

		if (!ref) {
			upload_data[i].idx = TILE_INDEX_NONE;
			continue;
		} 
		TileTexUploadData data = {
			.p_state = &m_pages[idx.page]->states[idx.tex],
			.offset = i*m_tile_size_bytes,
			.code = upload.code,
			.idx = idx,
			.data_ref = *ref
		};

		data.p_state->store(TILE_TEX_STATE_QUEUED);

		upload_data[i] = data;

		++ctx->refs;
		g_schedule_task([=](){
			tile_upload_fn(p_ctx, data);
		});
	}

	while (ctx->refs > 0) {
		std::this_thread::yield();
	}

	glBindBuffer(GL_PIXEL_UNPACK_BUFFER,ctx->pbo);
	for (size_t i = 0; i < upload_data.size(); ++i) {
		TileTexUploadData data = upload_data[i];

		if (!data.idx.is_valid()) {
			continue;
		}

		TileGPULoadState state = data.p_state->load(); 

		// TODO : Will probably need CAS here eventually? Keep in in mind.
		if (state == TILE_TEX_STATE_CANCELLED) {
			data.p_state->store(TILE_TEX_STATE_EMPTY);
			continue;
		}

		glTextureSubImage3D(
			m_pages[data.idx.page]->tex_array,
			0,
			0,0,
			data.idx.tex,
			TILE_WIDTH,
			TILE_WIDTH,
			1,
			m_img_format,
			m_data_type,
			(void*)(data.offset)
		);

		data.p_state->store(TILE_TEX_STATE_READY);
	}
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER,ctx->pbo);

	glUnmapNamedBuffer(ctx->pbo);
	glDeleteBuffers(1,&ctx->pbo);
}

void TileGPUCache::synchronous_upload(
	const TileCPUCache *data_cache,
	std::span<TileTexUpload> uploads 
)
{
	if (!uploads.size())
		return;

	log_info("uploading %ld tiles...",uploads.size());
	GLuint pbo;
	glCreateBuffers(1, &pbo);

	GLsizeiptr total_size = uploads.size()*m_tile_size_bytes;

	glNamedBufferStorage(
		pbo,
		total_size,
		nullptr,
		GL_MAP_WRITE_BIT | 
		GL_MAP_PERSISTENT_BIT | 
		GL_MAP_COHERENT_BIT | 
		GL_DYNAMIC_STORAGE_BIT
	);

	void* mapped = glMapNamedBufferRange(
		pbo, 
		0, 
		total_size, 
		GL_MAP_WRITE_BIT | 
		GL_MAP_PERSISTENT_BIT | 
		GL_MAP_COHERENT_BIT
	);

	for (size_t i = 0; i < uploads.size(); ++i) {
		TileTexUpload upload = uploads[i];

		size_t idx = upload.idx.tex;
		size_t offset = i*m_tile_size_bytes;
		size_t size = 0;

		uint8_t* dst = reinterpret_cast<uint8_t*>(mapped) + offset;
		const uint8_t *src = data_cache->acquire_block(upload.code,&size,nullptr);
		memcpy(dst,src,size);

		glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
		glTextureSubImage3D(
			m_pages[upload.idx.page]->tex_array,
			0,
			0,0,
			idx,
			TILE_WIDTH,
			TILE_WIDTH,
			1,
			m_img_format,
			m_data_type,
			(void*)offset
		);
	}
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);

	glUnmapNamedBuffer(pbo);

	glDeleteBuffers(1,&pbo);
}

void TileGPUCache::bind_texture_arrays(uint32_t base) const 
{
	assert(m_pages.size() <= MAX_TILE_PAGES);

	for (size_t i = 0; i < m_pages.size(); ++i) {
		glActiveTexture(GL_TEXTURE0 + base + i);
		glBindTexture(GL_TEXTURE_2D_ARRAY, m_pages[i]->tex_array);	
	}
}


