#ifndef TILE_CACHE_H
#define TILE_CACHE_H

#include "texture_loader.h"
#include "tiling.h"
#include "dataset.h"

// STL
#include <unordered_map>
#include <queue>
#include <list>
#include <span>

// libc
#include <cstdint>

static constexpr uint32_t TILE_WIDTH = 256;
static constexpr uint32_t TILE_SIZE = TILE_WIDTH*TILE_WIDTH;
static constexpr uint32_t TILE_PAGE_SIZE = 128;
static constexpr uint32_t MAX_TILE_PAGES = 16;
static constexpr uint32_t MAX_TILES = TILE_PAGE_SIZE*MAX_TILE_PAGES;

struct TilePage
{
	std::vector<uint16_t> free_list;
	GLuint tex_array;
};

struct TileTexIndex
{
	alignas(4)
	uint16_t page;
	uint16_t tex;
};

struct TileTexUpload
{
	TileCode code;
	TileTexIndex idx;
};
struct TileTexCache
{
	typedef std::list<std::pair<TileCode,TileTexIndex>> lru_list_t;

	// TODO : Robin hood hash table instead of this
	lru_list_t m_lru;
	std::unordered_map<
		TileCode, 
		lru_list_t::iterator,
		TileCodeHash
	> m_map;

	std::priority_queue<
		uint16_t, 
		std::vector<uint16_t>, 
		std::greater<uint16_t>
	> m_open_pages;

	std::vector<TilePage> m_pages;

	GLuint m_data_format = GL_R32F;
	GLuint m_img_format = GL_RED;
	GLuint m_data_type = GL_FLOAT;
	GLuint m_data_size = sizeof(float);

private:
	TileTexIndex allocate();
	void deallocate(TileTexIndex idx);
	void reserve(uint32_t count);

	TileTexIndex evict_one();

	void insert(TileCode, TileTexIndex);

public:
	TileTexCache();

	void get_textures(
		const std::span<TileCode> tiles, 
		std::vector<TileTexIndex>& textures,
		std::vector<TileTexUpload>& new_tiles
	);

	void bind_texture_arrays(uint32_t base) const;

	void synchronous_upload(const TileDataCache *data_cache,
		std::span<TileTexUpload> uploads);
};

static inline void test_upload(TileCode code, void *dst, void *usr)
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

			glm::dvec3 p = cube_to_globe(code.face, f);

			float c = 15;
			float g = sin(c*p.x)*cos(c*p.y)*
					  sin(c*p.z)*cos(c*p.z);

			data[idx++] = g;	
			uv.x += d;
		}
		uv.x = 0;
		uv.y += d;
	}

	return;
}



#endif //TILE_CACHE_H
