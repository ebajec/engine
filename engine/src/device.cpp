#include <engine/renderer/opengl.h>

#include "ev2/device.h"

#include "device_impl.h"

#include "asset_table.h"
#include "pool.h"

#include "stb_image.h"
	
static void init_gl(ev2::Device *dev)
{
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);       // makes callback synchronous

	glDebugMessageCallback([]( GLenum source,
							  GLenum type,
							  GLuint id,
							  GLenum severity,
							  GLsizei length,
							  const GLchar* message,
							  const void* userParam )
	{
		const char *fmt = 
			"GL DEBUG: [%u]\n"
			"    Source:   0x%x\n"
			"    Type:     0x%x\n"
			"    Severity: 0x%x\n"
			"    Message:  %s";

		if (type == GL_DEBUG_TYPE_ERROR) {
			log_error(fmt, id, source, type, severity, message);
		} else {
			log_info(fmt, id, source, type, severity, message);
		}
	}, nullptr);

	glDebugMessageControl(
		GL_DONT_CARE,          // source
		GL_DONT_CARE,          // type
		GL_DONT_CARE,          // severity
		0, nullptr,            // count + list of IDs
		GL_TRUE);              // enable

    glEnable(GL_MULTISAMPLE);

	const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* vendor = glGetString(GL_VENDOR);
    const GLubyte* glslVersion = glGetString(GL_SHADING_LANGUAGE_VERSION);

	log_info(
		"Successfully initiailzed OpenGL\n"
		"\tRenderer: %s\n"
		"\tOpenGL version: %s\n"
		"\tVendor: %s\n"
		"\tGLSL version: %s",
		renderer, version, vendor, glslVersion
	);
}

namespace ev2 {

Device *create_device(const char *path)
{
	stbi_set_flip_vertically_on_load(true);

	Device *dev = new Device{};

	init_gl(dev);

	dev->buffer_pool.reset(ResourcePool<Buffer>::create());
	dev->image_pool.reset(ResourcePool<Image>::create());
	dev->texture_pool.reset(ResourcePool<Texture>::create());

	size_t upload_capacity = (1 << 9) * (1 << 20);
	size_t upload_alignment = 512;

	dev->pool.reset(UploadPool::create(dev, 
		upload_capacity, 
		upload_alignment, 
		(1 << 14)
	));

	GLint64 ubo_offset_alignment;
	glGetInteger64v(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_offset_alignment);

	dev->transforms = GPUTTable<glm::mat4>((size_t)ubo_offset_alignment);
	dev->view_data = GPUTTable<ViewData>((size_t)ubo_offset_alignment);

	dev->assets.reset(AssetTable::create(dev, path));

	dev->frame.ubo = ev2::create_buffer(dev, sizeof(GPUFramedata), ev2::MAP_WRITE);

	dev->start_time_ns = 
		std::chrono::high_resolution_clock::now().time_since_epoch().count();

	glm::mat4 proj_def = glm::mat4(1.f);
	glm::mat4 view_def = glm::mat4(1.f);

	ViewData viewdata = view_data_from_matrices(
		glm::value_ptr(proj_def), glm::value_ptr(view_def));

	dev->default_view = EV2_HANDLE_CAST(View,dev->view_data.add(viewdata));

	return dev;
}

void destroy_device(Device *dev)
{
	dev->assets.reset();
	dev->pool.reset();

	dev->transforms.destroy(dev);
	dev->view_data.destroy(dev);

	delete dev;
}

//------------------------------------------------------------------------------
// UploadPool

UploadPool *UploadPool::create(Device *dev, size_t capacity, size_t align, size_t max_uploads)
{
	if (!is_pow2(align)) {
		log_error("Alignment of upload pool is not power of two: %d", align);
		return nullptr;
	}

	if (!is_pow2(capacity)) {
		log_error("Capacity of upload pool is not power of two: %d", capacity);
		return nullptr;
	}

	capacity = align_up_pow2(capacity, align);

	BufferID h_buf = create_buffer(dev, capacity, ev2::MAP_WRITE | ev2::MAP_PERSISTENT);

	if (EV2_IS_NULL(h_buf)) {
		log_error("Failed to create buffer for upload pool");
		return nullptr;
	}

	Buffer *buf = dev->get_buffer(h_buf);

	GLenum access = 
		GL_MAP_WRITE_BIT | 
		GL_MAP_PERSISTENT_BIT | 
		GL_MAP_FLUSH_EXPLICIT_BIT;

	void *mapped = glMapNamedBufferRange(buf->id, 0, capacity, access);

	if (!mapped) {
		log_error("Failed to map buffer for upload pool");
		return nullptr;
	}

	UploadPool *pool = new UploadPool{
		.max_uploads = max_uploads,
		.capacity = capacity,
		.align = align,
		.mapped = mapped,
		.entries = new entry_t[max_uploads],
		.buffer = h_buf,
		.dev = dev,
	};

	return pool;
}

void UploadPool::destroy(UploadPool *pool)
{
	if (!pool)
		return;

	ev2::Device *dev = pool->dev;

	Buffer *buf = dev->get_buffer(pool->buffer);
	glUnmapNamedBuffer(buf->id);

	ev2::destroy_buffer(dev, pool->buffer);

	delete[] pool->entries;
	delete pool;
}

static inline void record_buffer_copy(Buffer *src, Buffer *dst, 
						uint32_t count, const BufferUpload *regions)
{

	for (uint32_t i = 0; i < count; ++i) {
		BufferUpload region = regions[i];
		glCopyNamedBufferSubData(src->id, dst->id, 
						   region.src_offset, 
						   region.dst_offset, 
						   region.size
						   );
	}
}

static inline void record_image_copy(Buffer *src, Image *img, 
						uint32_t count, const ImageUpload *regions)
{
	const bool is_3d = img->d > 1;
	GLenum format, type;
	image_format_to_gl(img->fmt, &format, &type);

	glBindBuffer(GL_PIXEL_UNPACK_BUFFER,src->id); 

	for (size_t i = 0; i < count; ++i) {
		ImageUpload reg = regions[i];

		if (is_3d) {
			glTextureSubImage3D(
				img->id,
				reg.level,
				reg.x,
				reg.y,
				reg.z,
				reg.w,
				reg.h,
				reg.d,
				format,
				type,
				(void*)(reg.src_offset)
			);
	 	} else {
			glTextureSubImage2D(
				img->id,
				reg.level,
				reg.x,
				reg.y,
				reg.w,
				reg.h,
				format,
				type,
				(void*)(reg.src_offset)
			);
		}
	}

	glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
}

ev2::Result UploadPool::wait_for(uint64_t value)
{
	while (!epochs.empty()) {
		epoch_t epoch = epochs.top();

		if (value < epoch.id)
			return SUCCESS;

		uint64_t timeout_ns = 1e9;
		GLenum result = glClientWaitSync(epoch.sync, 0, timeout_ns); 

		if (result == GL_TIMEOUT_EXPIRED) {
			log_warn("Upload timed out: epoch=%d, size=%d", epoch.id);
			return ev2::TIMEOUT;
		} else if (result == GL_WAIT_FAILED) {
			log_error("Upload failed, aborting");
			return ev2::EUNKNOWN;
		}
		epochs.pop();
	}
	return SUCCESS;
}

void UploadPool::flush()
{
	uint32_t flush_tail = flush_idx;
	uint32_t flush_head = flush_tail;

	size_t flushed_bytes = 0;

	// TODO : Better synchrnozation that doesn't lock this
	// whole loop
	//
	// Another threads may increment the ring buffer but not
	// touch the queue for now. 
	std::unique_lock<std::mutex> lock(sync);

	// "Grabs" the largest commited range possible, starting from the tail
	for (uint32_t i = flush_idx; 
		i != head_idx && entries[i].done_value != 0; 
		i = (i + 1) & (max_uploads - 1)
	) {
		flushed_bytes += entries[i].size;
		++flush_head;

		assert(capacity - entries[i].start >= entries[i].size);
	}

	// If no uploads are commited, return early
	if (flush_head == flush_tail)
		return;

	std::swap(queues[1],queues[0]);

	flush_head &= (max_uploads - 1);
	flush_idx = flush_head;
	lock.unlock();

	size_t flush_start = entries[flush_tail].start;

	Buffer *src_buf = dev->get_buffer(buffer);

	if (flush_start + flushed_bytes < capacity) {
		glFlushMappedNamedBufferRange(src_buf->id, flush_start, flushed_bytes);
	} else {
		glFlushMappedNamedBufferRange(src_buf->id, flush_start, capacity - flush_start);
		glFlushMappedNamedBufferRange(src_buf->id, 0, (flushed_bytes + flush_start) - capacity);
	}

	size_t buf_idx = 0;
	size_t img_idx = 0;

	for (uint32_t i = flush_tail; i != flush_head; i = (i + 1) & (max_uploads - 1)) {
		entry_t *ent = &entries[i];

		uint32_t count = ent->count;

		switch (ent->type) {
			case UPLOAD_TYPE_BUFFER: {
				Buffer *dst_buf = dev->get_buffer(ent->resource.buf);
				const BufferUpload *regions = &queues[1].buffers[buf_idx]; 

				record_buffer_copy(src_buf, dst_buf, count, regions);

				buf_idx += count;
				break;
			} 
			case UPLOAD_TYPE_IMAGE: {
				Image *dst_img = dev->get_image(ent->resource.img);
				const ImageUpload *regions = &queues[1].images[img_idx]; 

				record_image_copy(src_buf, dst_img, count, regions);

				img_idx += count;
				break;
			}
		}
	}

	epoch_t epoch = {
		.id = done_ctr.fetch_add(flushed_bytes, std::memory_order_acquire) + flushed_bytes,
		.size = flushed_bytes,
		.sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0),
	};

	//log_info(
	//	"Flushed uploads:\n"
	//	"	epoch=%lld\n"
	//	"	size=%lld\n"
	//	"	start=%lld",
	//	(unsigned long long)epoch.id, 
	//	(unsigned long long)epoch.size, 
	//	(unsigned long long)flush_start
	//);

	epochs.push(epoch);

	queues[1].buffers.clear();
	queues[1].images.clear();

}

uint64_t UploadPool::set_commited(entry_t *ent)
{
	size_t size = ent->size;
	size_t done_value = size + timeline_ctr.fetch_add(size, std::memory_order_acquire); 
	ent->done_value = done_value;

	return done_value;
}

static inline uint32_t limit_count(uint32_t count)
{
	constexpr uint32_t max_regions = UINT16_MAX;
	if (count > max_regions) {
		count = max_regions;
		log_warn(
			"Upload region count (%d) beyond maximum; limiting to %d", 
		   count, max_regions
		);
	}

	return count;
}

uint64_t UploadPool::commmit_buffer(uint32_t idx, BufferID buf, const BufferUpload *regions, 
									uint32_t count)
{
	entry_t *ent = &entries[idx];

	count = limit_count(count);

	ent->count = count;
	ent->type = UPLOAD_TYPE_BUFFER;
	ent->resource.buf = buf;

	size_t start = ent->start;

	std::unique_lock<std::mutex> lock(sync);
	for (size_t i = 0; i < count; ++i) {
		BufferUpload region = regions[i];
		region.src_offset += start;
		queues[0].buffers.push_back(region);
	}

	return set_commited(ent);
}

uint64_t UploadPool::commmit_image(uint32_t idx, ImageID img, const ImageUpload *regions,
								   uint32_t count)
{
	entry_t *ent = &entries[idx];

	count = limit_count(count);

	ent->count = count;
	ent->type = UPLOAD_TYPE_IMAGE;
	ent->resource.img = img;

	size_t start = ent->start;

	std::unique_lock<std::mutex> lock(sync);
	for (size_t i = 0; i < count; ++i) {
		ImageUpload region = regions[i];
		region.src_offset += start;
		queues[0].images.push_back(region);
	}

	return set_commited(ent);
}

UploadPool::alloc_result_t UploadPool::alloc(size_t _bytes, size_t _align)
{
	static constexpr alloc_result_t ALLOC_FAILED = alloc_result_t{};

	size_t required = align_up_pow2(_bytes, align);
	size_t bias = align;

	// the bias could maybe help mitigate resource starvation
	if (required + bias > capacity) {
		log_warn("Upload too big for pool: size=%d bytes, capacity=%d bytes", required, capacity);
		return ALLOC_FAILED;
	}
	
	size_t available;

	do {
		// Three cases:
		//
		// ---h___t----
		//	   ^^^
		// _____t----h_
		// ^^^^^
		// _t----h_____
		//		  ^^^^^

		if (head == tail) {
			available = capacity;
			head = 0;
			tail = 0;
		} else {
			available = head < tail ? tail - head : std::max(tail, capacity - head); 
		}

		if (required < available) 
			break;

		if (head_idx == tail_idx) {
			log_error("This should never happen!");
			assert(false);
		}

		uint64_t wait_value = done_ctr.load(std::memory_order_acquire);

		if(!epochs.empty()) {
			epoch_t epoch = epochs.top();

			uint64_t timeout_ns = 1e9;
			GLenum result = glClientWaitSync(epoch.sync, 0, timeout_ns); 

			if (result == GL_TIMEOUT_EXPIRED) {
				log_warn("Upload timed out: epoch=%d, size=%d", epoch.id);
			} else if (result == GL_WAIT_FAILED) {
				log_error("Upload failed, aborting");
				return ALLOC_FAILED;
			}

			epochs.pop();
			wait_value = epoch.id;
		}

		if (entries[tail_idx].start != tail) {
			log_error("cap=%lld, entries[tail_idx].start=%lld, tail=%lld", 
			 	(unsigned long long)capacity,
				(unsigned long long)entries[tail_idx].start,
				(unsigned long long)tail);

			assert(entries[tail_idx].start == tail);
		}

		entry_t tail_entry;
		do {
			tail_entry = entries[tail_idx];
			tail_idx = (tail_idx + 1) & (max_uploads - 1);

			if (tail_idx == head_idx) { // entries is empty here
				tail = head;
				break;
			} else {
				tail = entries[tail_idx].start;
			}
		} while (tail_entry.done_value && tail_entry.done_value < wait_value);
	} while (true);

	if (capacity - head <= required) {
		size_t old_head = head;
		head = 0;
		
		// ensures that the tail seen in the entry list always matches a previos 
		// upload start
		if (tail == old_head)
			tail = 0;

		log_warn("Not enough room");
	}

	entry_t ent = {
		.start = head,
		.done_value = 0,
		.size = required,
		.count = 0,
		.type = {}
	};

	assert(capacity - ent.start >= ent.size);

	entries[head_idx] = ent;

	alloc_result_t res = {
		.ptr = (char*)mapped + head,
		.idx = head_idx
	};

	// note that required is always aligned properly, so head is after this as well
	head += required;
	head_idx = (head_idx + 1) & (max_uploads - 1); 

	return res;
}

};
