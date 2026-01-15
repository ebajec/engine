#ifndef DEVICE_INTERNAL_H
#define DEVICE_INTERNAL_H

#include "asset_table.h"
#include "pool.h"
#include "resource_impl.h"
#include "render_impl.h"

#include <algorithm>
#include <set>

#include <glm/mat4x4.hpp>

namespace ev2 {

static inline size_t align_up(size_t x, size_t alignment)
{
    return ((x + alignment - 1) / alignment) * alignment;
}

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

struct FrameContext
{
	double t; 
	double dt;
	uint32_t w, h;

	BufferID ubo;
};

struct Device
{
	// Assets
	std::unique_ptr<AssetTable> assets;

	// Resource pools
	std::unique_ptr<ResourcePool<Buffer>> buffer_pool;
	std::unique_ptr<ResourcePool<Image>> image_pool;
	std::unique_ptr<ResourcePool<Texture>> texture_pool;

	// Special GPU Resources
	GPUTTable<ViewData> view_data;
	GPUTTable<glm::mat4> transforms;

	// Frame data
	
	FrameContext frame;

	// convenience
	inline Buffer *get_buffer(BufferID h) {
		ResourceID rid = {.u64 = h.id};
		return buffer_pool->get(rid);
	}
	inline Image *get_buffer(ImageID h) {
		ResourceID rid = {.u64 = h.id};
		return image_pool->get(rid);
	}
	inline Texture *get_buffer(TextureID h) {
		ResourceID rid = {.u64 = h.id};
		return texture_pool->get(rid);
	}
};

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
