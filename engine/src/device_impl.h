#ifndef DEVICE_INTERNAL_H
#define DEVICE_INTERNAL_H

#include "asset_table.h"
#include "pool.h"
#include "resource_impl.h"
#include "render_impl.h"
#include "pipeline_impl.h"

#include <algorithm>
#include <set>
#include <mutex>
#include <queue>

#include <glm/mat4x4.hpp>

namespace ev2 {

static inline size_t align_up(size_t x, size_t alignment)
{
    return ((x + alignment - 1) / alignment) * alignment;
}

static inline size_t align_up_pow2(size_t x, size_t align)
{
    return (x + (align - 1)) & ~(align - 1);;
}

static inline constexpr bool is_pow2(size_t x)
{
	return !x || (((x - 1) & x) == 0);
}

static inline constexpr size_t mod_pow2(ptrdiff_t a, size_t b) {
	assert(is_pow2(b));
	return ((size_t)a) & (b - 1);
}

static_assert(mod_pow2(-3,4) == 1);
static_assert(mod_pow2(-6,8) == 2);
static_assert(1 + mod_pow2(-1,8) == 8);
static_assert(1 + mod_pow2(-1,16) == 16);


template<typename T>
struct GPUTTable
{
	std::vector<uint32_t> free;
	std::vector<T> data;

	// sizeof(T) aligned to some value
	size_t stride = 0;
	size_t capacity = 0;

	BufferID buffer = EV2_NULL_HANDLE(Buffer);

	// A better data structure could be used - this works for now
	std::set<uint32_t, std::less<uint32_t>> updates;

	//@brief allocate a new entry
	//@return id of entry
	uint32_t add(const T& val);

	// @brief free up an entry 
	// @return true if a valid id was passed
	bool remove(uint32_t idx);

	// @brief set a new value for an entry
	// @return true if a valid id was passed
	bool set(uint32_t idx, const T& mat);

	/// @brief Upload new data to the GPU if anything has changed.
	/// @return True if buffer was resized, false otherwise
	bool update(Device *dev);

	GPUTTable() = default;
	
	// @param alignment - alignment of data in GPU buffer
	GPUTTable(size_t _alignment);

	void destroy(Device *dev);
};

struct UploadPool
{
	enum UploadType 
	{
		UPLOAD_TYPE_BUFFER,
		UPLOAD_TYPE_IMAGE,
	};

	struct alloc_result_t
	{
		void *ptr;
		uint32_t idx;
	};

	struct epoch_t {
		uint64_t id;
		size_t size;
		GLsync sync;

		constexpr bool operator == (epoch_t other) const {
			return id == other.id;
		}
		constexpr bool operator > (epoch_t other) const {
			return id > other.id;
		}
	};

	struct entry_t
	{
		// set at allocation time
		size_t start;

		// set at commit time
		uint64_t done_value;

		union {
			BufferID buf;
			ImageID img;
		} resource = {};

		uint64_t size : 46;  // size in bytes of upload
		uint64_t count : 16; // number of upload regions
		uint64_t type : 2;   // upload type
	};

	size_t max_uploads;
	size_t capacity;
	size_t align;

	void *mapped;

	// head and tail indices of THE allocated range
	size_t head = 0, tail = 0;

	// tail index of non-flushed entries
	uint32_t flush_idx = 0; 

	// head and tail indices of allocated entries
	uint32_t head_idx = 0, tail_idx = 0;

	entry_t *entries;

	std::atomic_uint64_t done_ctr {};
	std::atomic_uint64_t timeline_ctr {};

	std::priority_queue<
		epoch_t, 
		std::vector<epoch_t>,
		std::greater<epoch_t>
	> epochs {};

	struct {
		std::vector<BufferUpload> buffers;
		std::vector<ImageUpload> images;
	} queues[2] {};

	mutable std::mutex sync{};

	BufferID buffer;
	Device *dev;

	//------------------------------------------------------------------------------ 
	//
	static UploadPool *create(Device *dev, size_t capacity, size_t align, size_t max_uploads);
	static void destroy(UploadPool *pool);

	alloc_result_t alloc(size_t _bytes, size_t _align);

	/// @note GPU uploads are performed in commit order
	
	uint64_t set_commited(entry_t *ent); 
	
	uint64_t commmit_buffer(uint32_t idx, BufferID buf, const BufferUpload *regions, uint32_t count);
	uint64_t commmit_image(uint32_t idx, ImageID buf, const ImageUpload *regions, uint32_t count);

	void flush();

	ev2::Result wait_for(uint64_t value);
};

struct GPUFramedata
{
	uint32_t t_seconds;
	float t_fract;
	float dt;
};

struct FrameContext
{
	double t; 
	double dt;
	uint32_t w, h;
	BufferID ubo;
};

struct Device
{
	std::unique_ptr<UploadPool, void(*)(UploadPool*)> pool = {
		nullptr, UploadPool::destroy
	};

	// Assets
	std::unique_ptr<AssetTable, void(*)(AssetTable*)> assets = {
		nullptr, AssetTable::destroy
	};

	// Resource pools
	std::unique_ptr<ResourcePool<Buffer>> buffer_pool;
	std::unique_ptr<ResourcePool<Image>> image_pool;
	std::unique_ptr<ResourcePool<Texture>> texture_pool;

	// Special GPU Resources
	GPUTTable<ViewData> view_data;
	GPUTTable<glm::mat4> transforms;

	// Frame data
	
	FrameContext frame;

	uint64_t start_time_ns;

	// convenience
	inline Buffer *get_buffer(BufferID h) {
		ResourceID rid = {.u64 = h.id};
		return buffer_pool->get(rid);
	}
	inline Image *get_image(ImageID h) {
		ResourceID rid = {.u64 = h.id};
		return image_pool->get(rid);
	}
	inline Texture *get_texture(TextureID h) {
		ResourceID rid = {.u64 = h.id};
		return texture_pool->get(rid);
	}

	inline GraphicsPipeline *get_gfx_pipeline(GraphicsPipelineID h) {
		AssetID id = static_cast<uint32_t>(h.id);
		AssetEntry *ent = assets->get_entry(id);
		return (GraphicsPipeline*)ent->usr;
	}
	inline ComputePipeline *get_compute_pipeline(ComputePipelineID h) {
		AssetID id = static_cast<uint32_t>(h.id);
		AssetEntry *ent = assets->get_entry(id);
		return (ComputePipeline*)ent->usr;
	}
	inline Shader *get_shader(ShaderID h) {
		AssetID id = static_cast<uint32_t>(h.id);
		AssetEntry *ent = assets->get_entry(id);
		return (Shader*)ent->usr;
	}
};

//------------------------------------------------------------------------------
// GPUTTable

template<typename T>
GPUTTable<T>::GPUTTable(size_t _alignment) : 
	stride(align_up(sizeof(T), _alignment)) {
}

template<typename T>
void GPUTTable<T>::destroy(Device *dev)
{
	if (!EV2_IS_NULL(buffer)) {
		destroy_buffer(dev, buffer);
	}
}

template<typename T>
uint32_t GPUTTable<T>::add(const T& mat)
{
	uint32_t idx;
	if (!free.empty()) {
		idx = free.back();
		free.pop_back();
		data[idx] = mat;
	} else {
		idx = (uint32_t)data.size();
		data.push_back(mat);
	}

	updates.insert(idx);

	return idx;
}

template<typename T>
bool GPUTTable<T>::remove(uint32_t idx)
{
	if (idx >= data.size()) {
		return false;
	}

	free.push_back(idx);

	return true;
}

template<typename T>
bool GPUTTable<T>::set(uint32_t idx, const T& mat)
{
	if (idx >= data.size()) {
		log_error("Invalid matrix cache index %d", idx);
		return false;
	}

	data[idx] = mat;

	updates.insert(idx);

	return true;
}

template<typename T>
bool GPUTTable<T>::update(Device *dev)
{
	bool resized = false;

	if (updates.empty())
		return false;

	size_t min_cap = std::max(data.size(),(size_t)4);

	if (capacity < min_cap) {
		//could make growth behaviour configurable 
		size_t growth = capacity ? capacity << 1 : 1;

		capacity = ((min_cap + growth - 1)/growth) * growth;

		if (buffer.id)
			destroy_buffer(dev, buffer);

		buffer = create_buffer(dev, 
						 capacity * stride, 
						 ev2::MAP_WRITE
						 );

		resized = true;
	}

	// TODO: Instead of a direct upload to the buffer read by the GPU, 
	// upload to a staging region and queue asynchronous uploads

	Buffer *buf = dev->buffer_pool->get(ResourceID{buffer.id});

	size_t start, count;

	if (resized) {
		start = 0;
		count = data.size();
	} else {
		start = *updates.begin();
		count = 1 + *updates.rbegin() - start;
	}

	char *mapped = (char*)glMapNamedBufferRange(
		buf->id, 
		start * stride, 
		count * stride, 
		GL_MAP_WRITE_BIT
	);

	for (uint32_t idx : updates) {
		char *dst = mapped + (idx - start)*stride;
		memcpy(dst, &data[idx], sizeof(T));
	}

	glUnmapNamedBuffer(buf->id);

	updates.clear();

	return resized;
}
};

#endif //DEVICE_INTERNAL_H
