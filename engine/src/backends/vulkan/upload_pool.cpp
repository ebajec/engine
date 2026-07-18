#include "upload_pool.h"
#include "ev2/utils/common.h"

#include "context.h"

#include <cassert>

namespace ev2 {

//------------------------------------------------------------------------------
// UploadPool

UploadPool *UploadPool::create(
	GfxContext *ctx, 
	QueueFamily *queue_family,
	size_t capacity,
	size_t align,
	size_t max_uploads
)
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

	VkSemaphoreTypeCreateInfo timelineCreateInfo;
	timelineCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
	timelineCreateInfo.pNext = NULL;
	timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	timelineCreateInfo.initialValue = 0;

	VkSemaphoreCreateInfo createInfo;
	createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	createInfo.pNext = &timelineCreateInfo;
	createInfo.flags = 0;

	VkSemaphore timeline;
	VkResult result = vkCreateSemaphore(ctx->device, &createInfo, NULL, &timeline);

	if (result != VK_SUCCESS) {
		log_error("Failed to create buffer for upload pool");
		return nullptr;
	}

	VkBuffer buffer;
	VmaAllocation allocation;

	VkBufferCreateInfo buf_ci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.pNext = nullptr,
		.flags = 0,
		.size = capacity,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	VmaAllocationCreateInfo alloc_ci = {
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
		.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 
		.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
		.memoryTypeBits = UINT32_MAX,
		.pool = nullptr,
		.minAlignment = align,
	};

	VmaAllocationInfo alloc_info;

	result = vmaCreateBuffer(ctx->allocator, 
								   &buf_ci, &alloc_ci, &buffer, 
								   &allocation, &alloc_info); 

	if (result != VK_SUCCESS) {
		log_error("Failed to create buffer for upload pool");
		return nullptr;
	}

	void *mapped = nullptr; 

	result = vkMapMemory(
		ctx->device, 
		alloc_info.deviceMemory, 
		alloc_info.offset, 
		alloc_info.size,
		0,
		&mapped
	);

	if (!mapped || result != VK_SUCCESS) {
		log_error("Failed to map buffer for upload pool");
		return nullptr;
	}

	VkCommandPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = queue_family->index,
	};

	VkCommandPool command_pool;

	result = vkCreateCommandPool(ctx->device, &pool_info, nullptr, &command_pool);

	if (result != VK_SUCCESS) {
		log_error("Failed to create command pool for upload pool");
		return nullptr;
	}

	UploadPool *pool = new UploadPool{
		.max_uploads = max_uploads,
		.capacity = capacity,
		.align = align,
		.mapped = mapped,
		.entries = new entry_t[max_uploads],

		.semaphore = timeline,

		.staging_buf = buffer,
		.allocation = allocation,
		.memory = alloc_info.deviceMemory,
		.memory_offset = alloc_info.offset,

		.command_pool = command_pool,

		.queue_family = queue_family,
		.ctx = ctx,
	};

	return pool;
}

void UploadPool::destroy(UploadPool *pool)
{
	if (!pool)
		return;

	ev2::GfxContext *ctx = pool->ctx;

	uint64_t done_value = pool->done_ctr.load(); 

	VkSemaphoreWaitInfo wait_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
		.semaphoreCount = 1,
		.pSemaphores = &pool->semaphore,
		.pValues = &done_value,
	};

	vkWaitSemaphores(ctx->device, &wait_info, 1e9);

	vkUnmapMemory(ctx->device,pool->memory);
	vmaDestroyBuffer(ctx->allocator, pool->staging_buf, pool->allocation);
	vkDestroySemaphore(ctx->device, pool->semaphore, nullptr);
	vkDestroyCommandPool(ctx->device, pool->command_pool, nullptr);

	delete[] pool->entries;
	delete pool;
}

static inline void record_buffer_copy(VkCommandBuffer cmds,
	VkBuffer staging, Buffer *dst, uint32_t count, const VkBufferCopy2 *regions)
{
	VkCopyBufferInfo2 copy_info = {
		.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
		.pNext = nullptr,
		.srcBuffer = staging,
		.dstBuffer = dst->buffer,
		.regionCount = count,
		.pRegions = regions,
	};
	
	vkCmdCopyBuffer2(cmds, &copy_info);  
}

static inline void record_image_copy(VkCommandBuffer cmds,
	VkBuffer staging, Image *img, uint32_t count, const VkBufferImageCopy2 *regions)
{
	VkImageLayout layout = img->state.get_current().layout;
	assert(layout);

	VkCopyBufferToImageInfo2 copy_info = {
		.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2,
		.pNext = nullptr,
		.srcBuffer = staging,
		.dstImage = img->image,
		.dstImageLayout = layout,
		.regionCount = count,
		.pRegions = regions,
	};
	
	vkCmdCopyBufferToImage2(cmds, &copy_info);  
}

ev2::Result UploadPool::wait_for(uint64_t value)
{
	while (!epochs.empty()) {
		epoch_t epoch = epochs.top();

		if (value < epoch.done_value)
			return SUCCESS;

		uint64_t timeout_ns = 1e9;

		VkSemaphore wait_semaphore = this->semaphore;

		VkSemaphoreWaitInfo wait_info = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
			.semaphoreCount = 1,
			.pSemaphores = &wait_semaphore,
			.pValues = &value,
		};

		VkResult result = vkWaitSemaphores(ctx->device, &wait_info, timeout_ns);

		if (result == VK_TIMEOUT) {
			log_warn("Upload timed out: epoch=%d, size=%d", epoch.done_value);
			return ev2::TIMEOUT;
		} else if (result != VK_SUCCESS) {
			log_error("Upload failed, aborting");
			return ev2::EUNKNOWN;
		}

		free_command_buffers.push_back(epoch.command_buffer);
		epochs.pop();
	}
	return SUCCESS;
}

void UploadPool::collect_finished_command_buffers()
{
	return;
}

VkCommandBuffer UploadPool::get_command_buffer()
{
	//-----------------------------------------------------------------------------
	// Reclaim command buffers from any previous submissions

	bool requires_new = true;
	VkCommandBuffer cmds = VK_NULL_HANDLE;
	VkResult result = VK_SUCCESS;

	if (free_command_buffers.empty()) {
		uint64_t semaphore_value;
		result = vkGetSemaphoreCounterValue(ctx->device, semaphore, &semaphore_value);

		if (result != VK_SUCCESS) {
			log_warn("Failed to read upload pool semaphore");
		} else {
			while (!epochs.empty()) {
				epoch_t epoch = epochs.top();
				if (epoch.done_value > semaphore_value)
					break;

				requires_new = false;
				cmds = epoch.command_buffer;
				epochs.pop();
				break;
			}
		}
	} else {
		cmds = free_command_buffers.back();
		free_command_buffers.pop_back();
		requires_new = false;
	}

	if (requires_new) {
		VkCommandBufferAllocateInfo alloc_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			 .commandPool = command_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		result = vkAllocateCommandBuffers(ctx->device, &alloc_info, &cmds);

		if (result != VK_SUCCESS) {
			log_error("Failed to allocate command buffer", cmds);
			return VK_NULL_HANDLE;
		}

		log_info("Allocated new VkCommandBuffer %x", cmds);
	} else {
		result = vkResetCommandBuffer(cmds, 0);

		if (result != VK_SUCCESS) {
			log_error("Failed to reset VkCommandBuffer %x", cmds);
			return VK_NULL_HANDLE;
		}
	}

	return cmds;
}
VkResult UploadPool::flush()
{
	//-----------------------------------------------------------------------------
	// Flush the memory ranges in mapped buffer
	
	uint32_t flush_tail = flush_idx;
	uint32_t flush_head = flush_tail;

	size_t flushed_bytes = 0;

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
		return VK_SUCCESS;

	std::swap(queues[1],queues[0]);

	flush_head &= (max_uploads - 1);
	flush_idx = flush_head;
	lock.unlock();

	size_t flush_start = entries[flush_tail].start;

	bool is_overflowed = flush_start + flushed_bytes > capacity; 

	if (flush_start + flushed_bytes == capacity) {
		log_error("Bad?");
	}

	uint32_t range_count = is_overflowed ? 2 : 1;
	VkMappedMemoryRange ranges[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
			.pNext = nullptr,
			.memory = memory,
			.offset = memory_offset + flush_start,
			.size = is_overflowed ? capacity - flush_start : flushed_bytes,
		},
		{
			.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
			.pNext = nullptr,
			.memory = memory,
			.offset = memory_offset,
			.size = (flushed_bytes + flush_start) - capacity,
		}
	};

	VkResult result = vkFlushMappedMemoryRanges(ctx->device, range_count, ranges);

	if (result != VK_SUCCESS) {
		log_error("Failed to flush upload staging buffer");
		return result;
	}

	std::vector<VkImageMemoryBarrier2> image_barriers;
	std::vector<VkBufferMemoryBarrier2> buffer_barriers;

	for (uint32_t i = flush_tail; i != flush_head; i = (i + 1) & (max_uploads - 1)) {
		constexpr VkPipelineStageFlags2 dst_stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		constexpr VkAccessFlags2 dst_access = VK_ACCESS_2_TRANSFER_WRITE_BIT;

		entry_t *ent = &entries[i];
		if (ent->has_semaphore)
			continue;

		switch (ent->type) {
			case UPLOAD_TYPE_BUFFER: {
				Buffer *dst_buf = ctx->get_buffer(ent->resource.to_buffer());
				buffer_barriers.push_back(VkBufferMemoryBarrier2{
					.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
					.srcStageMask = ent->prev_state.stage,
					.srcAccessMask = ent->prev_state.access,

					.dstStageMask = dst_stage,
					.dstAccessMask = dst_access,
					
					.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

					.buffer = dst_buf->buffer,
					.offset = 0,
					.size = dst_buf->size,
				});
				break;
			} 
			case UPLOAD_TYPE_IMAGE: {
				Image *dst_img = ctx->get_image(ent->resource.to_image());
				image_barriers.push_back(VkImageMemoryBarrier2{
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
					.srcStageMask = ent->prev_state.stage,
					.srcAccessMask = ent->prev_state.access,

					.dstStageMask = dst_stage,
					.dstAccessMask = dst_access,

					.oldLayout = ent->prev_state.layout,
					.newLayout = VK_IMAGE_LAYOUT_GENERAL,

					.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

					.image = dst_img->image,
					.subresourceRange = VkImageSubresourceRange{
						.aspectMask = dst_img->aspect_mask,
						.baseMipLevel = 0,
						.levelCount = dst_img->levels,
						.baseArrayLayer = 0,
						.layerCount = dst_img->d,
					}
				});
				break;
			}
		}
	}

	//-----------------------------------------------------------------------------
	// Record and submit the uploads
	
	VkCommandBuffer command_buffer = get_command_buffer();

	VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.pNext = nullptr,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = nullptr
	};

	result = vkBeginCommandBuffer(command_buffer, &begin_info);
	if (result != VK_SUCCESS)
		return result;

	VkDependencyInfo dependency_info = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.bufferMemoryBarrierCount = (uint32_t)buffer_barriers.size(),
		.pBufferMemoryBarriers = buffer_barriers.data(),
		.imageMemoryBarrierCount  = (uint32_t)image_barriers.size(),
		.pImageMemoryBarriers = image_barriers.data(),
	};

	vkCmdPipelineBarrier2(command_buffer, &dependency_info);

	size_t buf_idx = 0;
	size_t img_idx = 0;

	for (uint32_t i = flush_tail; i != flush_head; i = (i + 1) & (max_uploads - 1)) {
		entry_t *ent = &entries[i];
		uint32_t count = ent->count;

		switch (ent->type) {
			case UPLOAD_TYPE_BUFFER: {
				Buffer *dst_buf = ctx->get_buffer(ent->resource.to_buffer());
				const VkBufferCopy2 *regions = &queues[1].buffers[buf_idx]; 

				record_buffer_copy(command_buffer, staging_buf, dst_buf, count, regions);

				buf_idx += count;
				break;
			} 
			case UPLOAD_TYPE_IMAGE: {
				Image *dst_img = ctx->get_image(ent->resource.to_image());
				const VkBufferImageCopy2 *regions = &queues[1].images[img_idx]; 

				record_image_copy(command_buffer, staging_buf, dst_img, count, regions);

				img_idx += count;
				break;
			}
		}
	}

	result = vkEndCommandBuffer(command_buffer);
	if (result != VK_SUCCESS)
		return result;

	uint64_t prev_value = done_ctr.fetch_add(flushed_bytes, std::memory_order_acquire); 
	uint64_t done_value = prev_value + flushed_bytes; 

	VkCommandBufferSubmitInfo cmd_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = command_buffer
	};

	VkSemaphoreSubmitInfo signal_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = semaphore,
		.value = done_value,
		.stageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
		.deviceIndex = 0,
	};

	queues[1].waits.push_back(VkSemaphoreSubmitInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = semaphore,
		.value = prev_value,
		.stageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
		.deviceIndex = 0,
	});

	VkSubmitInfo2 submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,

		.flags = 0,

		.waitSemaphoreInfoCount = (uint32_t)queues[1].waits.size(),
		.pWaitSemaphoreInfos = queues[1].waits.data(),

		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmd_info,

		.signalSemaphoreInfoCount = 1,
		.pSignalSemaphoreInfos = &signal_info,
	};

	result = queue_family->queues[0].submit(
		1, &submit_info, VK_NULL_HANDLE
	);

	if (result != VK_SUCCESS)
		return result;

	epoch_t epoch = {
		.done_value = done_value,
		.size = flushed_bytes,
		.sync = done_value,
		.command_buffer = command_buffer
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
	queues[1].waits.clear();

	return result;
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

uint64_t UploadPool::post_commit_sync(entry_t *ent, ResourceState *state)
{
	size_t size = ent->size;
	size_t done_value = size + timeline_ctr.fetch_add(size, std::memory_order_acquire); 
	ent->done_value = done_value;

	VkPipelineStageFlags2 dst_stage = VK_PIPELINE_STAGE_2_COPY_BIT; 
	VkAccessFlags2 dst_access = VK_ACCESS_2_TRANSFER_WRITE_BIT; 

	ResourceStateFlags old_state = state->set_write(
		dst_access,
		dst_stage,
		queue_family->index
	);

	std::vector<ResourceSync> syncs;
	state->get_wait_syncs_for_write(syncs);

	uint32_t sync_count = (uint32_t)syncs.size();

	const bool has_semaphore = sync_count && old_state.queue_family_index == queue_family->index; 

	ent->prev_state = old_state;
	ent->has_semaphore = has_semaphore;

	if (has_semaphore) {
		for (uint32_t i = 0; i < sync_count; ++i) {
			assert(syncs[i].semaphore);
			queues[0].waits.push_back(VkSemaphoreSubmitInfo{
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
				.semaphore = syncs[i].semaphore,
				.value = syncs[i].wait_value,
				.stageMask = dst_stage,
				.deviceIndex = 0, 
			});
		}
	}
	state->sync_write(semaphore, done_value);
	state->last_used_by_frame = ctx->frame_counter;

	return done_value;
}

uint64_t UploadPool::commit_buffer(uint32_t idx, BufferID buf, const BufferUpload *regions, 
									uint32_t count)
{
	entry_t *ent = &entries[idx];

	count = limit_count(count);

	ent->count = count;
	ent->type = UPLOAD_TYPE_BUFFER;
	ent->resource = buf;

	size_t start = ent->start;

	std::unique_lock<std::mutex> lock(sync);
	for (size_t i = 0; i < count; ++i) {
		VkBufferCopy2 copy = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
			.pNext = nullptr,
			.srcOffset = start + regions[i].src_offset,
			.dstOffset = regions[i].dst_offset,
			.size = regions[i].size,
		};
		queues[0].buffers.push_back(copy);
	}

	Buffer *buffer = ctx->get_buffer(buf);

	uint64_t done_value = post_commit_sync(ent, &buffer->state);

	return done_value;
}

uint64_t UploadPool::commit_image(uint32_t idx, ImageID img, const ImageUpload *regions,
								   uint32_t count)
{
	entry_t *ent = &entries[idx];

	count = limit_count(count);

	ent->count = count;
	ent->type = UPLOAD_TYPE_IMAGE;
	ent->resource = img;

	size_t start = ent->start;

	std::unique_lock<std::mutex> lock(sync);
	for (size_t i = 0; i < count; ++i) {
		const Image * image = ctx->get_image(img);

		assert(image->aspect_mask);

		VkBufferImageCopy2 copy = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
			.pNext = nullptr,
			.bufferOffset = regions[i].src_offset + start,
			.bufferRowLength= 0,
			.bufferImageHeight = 0,
			.imageSubresource = VkImageSubresourceLayers{
				.aspectMask = image->aspect_mask,
				.mipLevel = regions[i].level,
				.baseArrayLayer = regions[i].z,
				.layerCount = regions[i].d,
			},
			.imageOffset = VkOffset3D{
				.x = (int32_t)regions[i].x,
				.y = (int32_t)regions[i].y,
				.z = (int32_t)regions[i].z,
			},
			.imageExtent = VkExtent3D{
				.width = regions[i].w,
				.height = regions[i].h,
				.depth = regions[i].d,
			},
		};
		queues[0].images.push_back(copy);
	}

	Image *image = ctx->get_image(img);

	uint64_t done_value = post_commit_sync(ent, &image->state);

	return done_value;
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

		//----------------------------------------------------------------------------
		// This is when we run out of space

		if (head_idx == tail_idx) {
			log_error("This should never happen!");
			assert(false);
		}

		uint64_t wait_value = done_ctr.load(std::memory_order_acquire);

		if(!epochs.empty()) {
			epoch_t epoch = epochs.top();
			wait_value = epoch.done_value;

			VkSemaphoreWaitInfo wait_info = {
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
				.flags = 0,
				.semaphoreCount = 1,
				.pSemaphores = &this->semaphore,
				.pValues = &wait_value
			};

			VkResult result = vkWaitSemaphores(ctx->device, &wait_info, 
									  EV2_UPLOAD_TIMEOUT);

			if (result == VK_TIMEOUT) {
				log_warn("Upload timed out: epoch=%d, size=%d", epoch.done_value);
			} else if (result != VK_SUCCESS) {
				log_error("Upload failed, aborting");
				return ALLOC_FAILED;
			}

			free_command_buffers.push_back(epoch.command_buffer);
			epochs.pop();
			wait_value = epoch.done_value;
		}

		if (entries[tail_idx].start != tail) {
			log_error("cap=%lld, entries[tail_idx].start=%lld, tail=%lld, head=%lld", 
			 	(unsigned long long)capacity,
				(unsigned long long)entries[tail_idx].start,
				(unsigned long long)tail,
				(unsigned long long)head
			 );

			//assert(entries[tail_idx].start == tail);
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

