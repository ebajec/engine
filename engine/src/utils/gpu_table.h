#include <ev2/resource.h>
#include <ev2/utils/log.h>

#include "utils/common.h"

#include <vector>

namespace ev2 {

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
	//std::set<uint32_t, std::less<uint32_t>> updates;

	std::vector<uint32_t> updates;

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

	updates.push_back(idx);

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

	updates.push_back(idx);

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

	uint32_t count = resized ? 
		static_cast<uint32_t>(data.size()) :
		static_cast<uint32_t>(updates.size());

	size_t upload_size = count * sizeof(T);

	ev2::UploadContext uc = ev2::begin_upload(dev, upload_size, stride);
	std::vector<ev2::BufferUpload> uploads(count);

	// TODO: Consolidate...
	for (size_t i = 0; i < count; ++i) {
		size_t idx = resized ? i : updates[i];

		((T*)uc.ptr)[i] = data[idx];
		uploads[i] = {
			.src_offset = i*sizeof(T),
			.dst_offset = idx*stride,
			.size = sizeof(T)
		};
	}

	uint64_t sync = ev2::commit_buffer_uploads(dev, uc, buffer, 
											uploads.data(), uploads.size());
	// TODO: bad bad bad bad bad get rid of this asap
	ev2::wait_complete(dev, sync);

	updates.clear();

	return resized;
}

};
