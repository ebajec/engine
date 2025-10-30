#include "renderer/gl_debug.h"

#include "utils/thread_pool.h"
#include "utils/log.h"

#include "globe/gpu_cache.h"

#include <thread>
#include <cstring>

struct GPUUploadContext
{
	GLuint pbo;
	std::atomic_int refs; 
	void* mapped;
	size_t cap;
};

static GPUUploadContext *upload_context_create(size_t cap)
{
	GPUUploadContext *ctx  = new GPUUploadContext{};
	glCreateBuffers(1,&ctx->pbo);
	ctx->refs = 0;
	ctx->cap = cap;
	
	glNamedBufferStorage(
		ctx->pbo,
		(GLsizeiptr)cap,
		nullptr,
		GL_MAP_WRITE_BIT | 
		GL_MAP_PERSISTENT_BIT | 
		GL_MAP_COHERENT_BIT | 
		GL_DYNAMIC_STORAGE_BIT
	);

	if (gl_check_err())
		goto failure;

	ctx->mapped = glMapNamedBufferRange(
		ctx->pbo, 
		0, 
		(GLsizeiptr)cap, 
		GL_MAP_WRITE_BIT | 
		GL_MAP_PERSISTENT_BIT | 
		GL_MAP_COHERENT_BIT
	);

	if (gl_check_err())
		goto failure;

	return ctx;

failure:
	if (ctx->pbo) glDeleteBuffers(1,&ctx->pbo);
	delete ctx;
	return nullptr;
}

static void upload_context_destroy(GPUUploadContext *ctx)
{
	assert(ctx);

	glUnmapNamedBuffer(ctx->pbo);
	glDeleteBuffers(1,&ctx->pbo);

	delete ctx;
}


static std::unique_ptr<TileGPUPage> create_page(GLuint format)
{
	std::unique_ptr<TileGPUPage> page (new TileGPUPage{});

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

static void destroy_page(TileGPUPage &page)
{
	glDeleteTextures(1,&page.tex_array);
}

GPUTileCache *GPUTileCache::create()
{
	GPUTileCache * cache = new GPUTileCache{};

	GLuint tex;
	glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &tex);
	glTextureStorage3D(tex, 1, cache->m_gl_tex_format,1 ,1, 1);
	glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	cache->m_default_tex_array = tex;

	return cache;
}

GPUTileCache::~GPUTileCache()
{
	glDeleteTextures(1, &m_default_tex_array);
	for (auto &page : m_pages) {
		destroy_page(*page);
	}
}

void GPUTileCache::deallocate(TileGPUIndex idx)
{
	TileGPUPage *page = m_pages[idx.page].get();

	if (page->free_list.empty()) {
		m_open_pages.push(idx.page);
	}

	page->free_list.push_back(idx.tex);
}

void GPUTileCache::reserve(uint32_t count)
{
	assert(count <= MAX_TILES);

	size_t curr = m_pages.size() * TILE_PAGE_SIZE;

	if (curr >= count)
		return;

	size_t req = (count ? count - 1 : 0)/TILE_PAGE_SIZE + 1;
	size_t diff = req - m_pages.size();

	m_pages.reserve(req);

	for (size_t i = 0; i < diff; ++i) {
		m_pages.emplace_back(create_page(m_gl_tex_format));
		m_open_pages.push(static_cast<uint16_t>(m_pages.size() - 1));
	}
}

TileGPUIndex GPUTileCache::allocate()
{
	if (m_open_pages.empty()) {
		m_pages.emplace_back(create_page(m_gl_tex_format));
		m_open_pages.push(static_cast<uint16_t>(m_pages.size() - 1));
	}

	uint16_t page_idx = m_open_pages.top(); 

	assert(page_idx < m_pages.size());

	TileGPUPage *page = m_pages[page_idx].get();

	uint16_t tex_idx = page->free_list.back();

	assert(tex_idx < TILE_PAGE_SIZE);

	page->free_list.pop_back();

	if (page->free_list.empty()) {
		m_open_pages.pop();
	}

	return TileGPUIndex{
		.page = page_idx,
		.tex = tex_idx,
	};
}

TileGPUIndex GPUTileCache::evict_one()
{
	assert(!m_lru.empty());
	
	std::pair<TileCode, TileGPUIndex> ent = m_lru.back();

	TileGPUIndex idx = ent.second;

	std::atomic<TileGPULoadState> *tex_state = &m_pages[idx.page]->states[idx.tex]; 

	TileGPULoadState state = tex_state->load(std::memory_order_relaxed);
	do {
		if (state == TILE_GPU_STATE_CANCELLED) 
			return TILE_GPU_INDEX_NONE;
		if (state == TILE_GPU_STATE_UPLOADING || state == TILE_GPU_STATE_QUEUED) {
			tex_state->store(TILE_GPU_STATE_CANCELLED);
			return TILE_GPU_INDEX_NONE;
		}
	} while(!tex_state->compare_exchange_weak(state, TILE_GPU_STATE_EMPTY,
										   std::memory_order_acquire, std::memory_order_relaxed));

	//log_info("Evicted tile %d from GPU cache",ent.first);

	m_lru.pop_back();
	m_map.erase(tile_code_pack(ent.first));

	return ent.second;
}

void GPUTileCache::insert(TileCode code, TileGPUIndex idx)
{
	uint64_t packed = tile_code_pack(code);
	assert(m_map.find(packed) == m_map.end());

	m_lru.push_front({code,idx});
	m_map[packed] = m_lru.begin();
}

size_t GPUTileCache::update(
	CPUTileCache const *source,
	const std::span<TileCode> loaded_tiles, 
	std::vector<TileGPUIndex>& textures
)
{
	size_t tile_count = std::min(loaded_tiles.size(),(size_t)MAX_TILES);

	reserve(static_cast<uint32_t>(tile_count));

	std::vector<TileGPUUploadData> upload_data;
	size_t offset = 0;

	for (size_t i = 0; i < tile_count; ++i) {
		TileCode code = loaded_tiles[i];

		if (code == TILE_CODE_NONE) {
			textures.push_back(TILE_GPU_INDEX_NONE);
			continue;
		}

		auto it = m_map.find(tile_code_pack(code));

		const bool found = it != m_map.end();

		TileGPUIndex idx = TILE_GPU_INDEX_NONE;

		if (found) {
			lru_list_t::iterator ent = it->second;
			m_lru.splice(m_lru.begin(), m_lru, ent);
			idx = ent->second;
		} else {
			tc_ref ref;

			if (tc_acquire(source->tc, code, &ref) != TC_OK) {
				log_warn("Failed to queue upload for incomplete tile %d", code);
				textures.push_back(idx);
				continue;
			} 

			idx = (m_map.size() >= MAX_TILES) ? 
				evict_one() : allocate();

			if (idx.is_valid()) {
				insert(code, idx);

				std::atomic<TileGPULoadState> * p_state = &m_pages[idx.page]->states[idx.tex]; 
				TileGPUUploadData data = {
					.data_ref = ref,
					.p_state = p_state,
					.offset = offset,
					.code = code,
					.idx = idx,
				};

				data.p_state->store(TILE_GPU_STATE_QUEUED);
				upload_data.push_back(data);

				offset += m_tile_size_bytes;
			} else {
				tc_release(ref);
			}
		}

		textures.push_back(idx);
	}

	asynchronous_upload(upload_data);

	return upload_data.size();
}

static void tile_upload_fn(
	GPUUploadContext *ctx,
	TileGPUUploadData data
)
{
	tc_ref ref = data.data_ref;
	// Should always be valid if it got this far.
	assert(ref.data && ref.p_state);

	uint8_t* dst = reinterpret_cast<uint8_t*>(ctx->mapped) + data.offset; 

	TileGPULoadState gpu_state = data.p_state->load(std::memory_order_relaxed);
	do {
		if (gpu_state == TILE_GPU_STATE_CANCELLED) {
			log_info("tile_upload_fn : Cancelled upload");
			goto cleanup;
		}
		if (gpu_state != TILE_GPU_STATE_QUEUED) {
			log_error("tile_upload_fn : Invalid tile state");
			goto cleanup;
		}
	} while (!data.p_state->compare_exchange_weak(gpu_state, TILE_GPU_STATE_UPLOADING,
											   std::memory_order_acquire, std::memory_order_relaxed));

	memcpy(dst,ref.data,ref.size);

cleanup:
	tc_release(ref);
	--ctx->refs;
	return;
}

//------------------------------------------------------------------------------
// Loading

void GPUTileCache::asynchronous_upload(std::span<TileGPUUploadData> upload_data)
{
	if (!upload_data.size())
		return;

	size_t total_size = upload_data.size()*m_tile_size_bytes;

	std::unique_ptr<GPUUploadContext,decltype(&upload_context_destroy)> ctx (
		upload_context_create(total_size),upload_context_destroy);

	GPUUploadContext* p_ctx = ctx.get();
	for (const TileGPUUploadData& tmp_data : upload_data) {
		TileGPUUploadData data = tmp_data;

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
		TileGPUUploadData data = upload_data[i];

		if (!data.idx.is_valid()) {
			log_error("Invalid index in queued texture uploads (this should never happen)");
			continue;
		}

		TileGPULoadState state = data.p_state->load(); 

		// TODO : Will probably need CAS here eventually? Keep in in mind.
		if (state == TILE_GPU_STATE_CANCELLED) {
			data.p_state->store(TILE_GPU_STATE_EMPTY);
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
			m_gl_img_format,
			m_gl_data_type,
			(void*)(data.offset)
		);
		data.p_state->store(TILE_GPU_STATE_READY);
	}
}

/*
void TileGPUCache::synchronous_upload(
	const TileCacheSegment *data_cache,
	std::span<TileTexUpload> uploads 
)
{
	if (!uploads.size())
		return;

	log_info("uploading %ld tiles...",uploads.size());
	GLuint pbo;
	glCreateBuffers(1, &pbo);

	GLsizeiptr total_size = static_cast<GLsizeiptr>(uploads.size()*m_tile_size_bytes);

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
			(GLint)idx,
			TILE_WIDTH,
			TILE_WIDTH,
			1,
			m_gl_img_format,
			m_gl_data_type,
			(void*)offset
		);
	}
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);

	glUnmapNamedBuffer(pbo);

	glDeleteBuffers(1,&pbo);
}
*/

void GPUTileCache::bind_textures(const RenderContext &ctx, uint32_t base) const 
{
	assert(m_pages.size() <= MAX_TILE_PAGES);

	for (size_t i = 0; i < m_pages.size(); ++i) {
		if (!m_pages[i]->tex_array) {
			log_error("Globe texture array %d has id 0!", i);
		}
		glActiveTexture(GL_TEXTURE0 + (GLenum)(base + i));
		glBindTexture(GL_TEXTURE_2D_ARRAY, m_pages[i]->tex_array);	
	}

	// Bind remaining spots with dummy values
	for (size_t i = m_pages.size(); i < MAX_TILE_PAGES; ++i) {
		glActiveTexture(GL_TEXTURE0 + (GLenum)(base + i));
		glBindTexture(GL_TEXTURE_2D_ARRAY, m_default_tex_array);	
	}
}


