#ifndef TILE_CACHE_H
#define TILE_CACHE_H

#include <ev2/context.h>
#include <ev2/globe/tiling.h>

#include "backends/opengl/def_opengl.h"

#include "terrain.h"

// STL
#include <memory>
#include <unordered_map>
#include <queue>
#include <list>
#include <span>

// libc
#include <cstdint>
#include <atomic>

static constexpr uint32_t TILE_PAGE_SIZE = 128;
static constexpr uint32_t MAX_TILE_PAGES = 8;
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
	tc_ref data_ref;
	std::atomic<TileGPULoadState> *p_state;
	size_t offset;
	tile_code_t code;
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

struct GPUTileCache
{
	typedef std::list<std::pair<tile_code_t,TileGPUIndex>> lru_list_t;

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

	ev2::Context * dev;

	GLuint m_gl_tex_format = GL_R32F;
	GLuint m_gl_img_format = GL_RED;
	GLuint m_gl_data_type = GL_FLOAT;
	GLuint m_data_size = sizeof(float);
	GLuint m_tile_size_bytes = sizeof(float)*TILE_SIZE;

	GLuint m_default_tex_array;

	~GPUTileCache();
private:
	TileGPUIndex evict_one();
	TileGPUIndex allocate();

	void deallocate(TileGPUIndex idx);
	void reserve(uint32_t count);
	void insert(tile_code_t, TileGPUIndex);
	void asynchronous_upload(std::span<TileGPUUploadData> upload_data);

public:
	static GPUTileCache *create();

	size_t update(
		CPUTileCache const *source,
		const std::span<tile_code_t> tiles, 
		std::vector<TileGPUIndex>& textures
	);
	void bind_textures(uint32_t base) const;

	/*
	void synchronous_upload(const TileCacheSegment *data_cache,
		std::span<TileTexUpload> uploads);
	*/
};

#endif //TILE_CACHE_H
