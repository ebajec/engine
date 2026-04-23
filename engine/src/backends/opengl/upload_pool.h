#ifndef EV2_UPLOAD_POOL_H
#define EV2_UPLOAD_POOL_H

#include "ev2/resource.h"
#include "backends/opengl/def_opengl.h"

#include <queue>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <vector>
#include <cassert>

namespace ev2 {

typedef struct Device Device;

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

};

#endif
