#ifndef TILE_CACHE_H
#define TILE_CACHE_H

#include "renderer/opengl.h"

#include "tiling.h"
#include "cpu_cache.h"

// STL
#include <unordered_map>
#include <queue>
#include <list>
#include <span>

// libc
#include <cstdint>
#include <atomic>

static constexpr uint32_t TILE_PAGE_SIZE = 128;
static constexpr uint32_t MAX_TILE_PAGES = 16;
static constexpr uint32_t MAX_TILES = TILE_PAGE_SIZE*MAX_TILE_PAGES;

struct TileGPUIndex
{
	alignas(4)
	uint16_t page;
	uint16_t tex;

	constexpr bool is_valid() const {
		return page != UINT16_MAX && tex != UINT16_MAX;
	}
};

static TileGPUIndex TILE_GPU_INDEX_NONE = {UINT16_MAX,UINT16_MAX};

enum TileGPULoadState
{
	TILE_GPU_STATE_EMPTY,
	TILE_GPU_STATE_READY,
	TILE_GPU_STATE_QUEUED,
	TILE_GPU_STATE_UPLOADING,
	TILE_GPU_STATE_CANCELLED
};

struct TileGPUUploadData
{
	TileDataRef data_ref;
	std::atomic<TileGPULoadState> *p_state;
	size_t offset;
	TileCode code;
	TileGPUIndex idx;
};

struct TileTexUpload
{
	TileCode code;
	TileGPUIndex idx;
};

struct TileGPUPage
{
	std::atomic<TileGPULoadState> states[TILE_PAGE_SIZE];
	std::vector<uint16_t> free_list;
	GLuint tex_array;
};

struct TileGPUCache
{
	typedef std::list<std::pair<TileCode,TileGPUIndex>> lru_list_t;

	// TODO : Robin hood hash table instead of this
	lru_list_t m_lru;
	std::unordered_map<
		uint64_t, 
		lru_list_t::iterator
	> m_map;

	std::priority_queue<
		uint16_t, 
		std::vector<uint16_t>, 
		std::greater<uint16_t>
	> m_open_pages;

	std::vector<std::unique_ptr<TileGPUPage>> m_pages;

	GLuint m_data_format = GL_R32F;
	GLuint m_img_format = GL_RED;
	GLuint m_data_type = GL_FLOAT;
	GLuint m_data_size = sizeof(float);
	GLuint m_tile_size_bytes = sizeof(float)*TILE_SIZE;

	~TileGPUCache();
private:
	TileGPUIndex evict_one();
	TileGPUIndex allocate();

	void deallocate(TileGPUIndex idx);
	void reserve(uint32_t count);
	void insert(TileCode, TileGPUIndex);
	void asynchronous_upload(const TileCPUCache *cpu_cache,
		std::span<TileGPUUploadData> upload_data);

public:
	size_t update(
		const TileCPUCache *cpu_cache,
		const std::span<TileCode> tiles, 
		std::vector<TileGPUIndex>& textures
	);

	void bind_texture_arrays(uint32_t base) const;

	void synchronous_upload(const TileCPUCache *data_cache,
		std::span<TileTexUpload> uploads);
};

#endif //TILE_CACHE_H
