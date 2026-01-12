#include "ev2/device.h"

#include "device_impl.h"

#include "asset_table.h"
#include "pool.h"

#include "stb_image.h"

#include <algorithm>

namespace ev2 {

Device *create_device(const char *path)
{
	Device *dev = new Device{};

	dev->assets.reset(AssetTable::create(dev, path));

	dev->buffers.reset(ResourcePool<Buffer>::create());
	dev->images.reset(ResourcePool<Image>::create());
	dev->textures.reset(ResourcePool<Texture>::create());

	stbi_set_flip_vertically_on_load(true);

	return dev;
}

void destroy_device(Device *dev)
{
	delete dev;
}

uint32_t MatrixCache::add_matrix(const glm::mat4& mat)
{
	uint32_t idx;
	if (!free.empty()) {
		idx = free.back();
		free.pop_back();
		matrices[idx] = mat;
	} else {
		idx = (uint32_t)matrices.size();
		matrices.push_back(mat);
	}

	u_start = std::min(u_start, idx);
	u_end = std::max(u_end, idx + 1);

	return idx;
}

void MatrixCache::remove_matrix(uint32_t idx)
{
	free.push_back(idx);
}

void MatrixCache::set_matrix(uint32_t idx, const glm::mat4& mat)
{
	if (idx >= matrices.size()) {
		log_error("Invalid matrix cache index %d", idx);
		return;
	}

	matrices[idx] = mat;

	u_start = std::min(u_start, idx);
	u_end = std::max(u_end, idx + 1);
}

bool MatrixCache::update(Device *dev)
{
	bool resized = false;

	if (u_start >= u_end)
		return false;

	if (capacity < matrices.size()) {
		capacity = capacity > 4 ? (capacity*3)/2 : 4;

		if (buffer.id)
			destroy_buffer(dev, buffer);

		buffer = create_buffer(dev, capacity * sizeof(glm::mat4));

		resized = true;
	}

	// TODO : Check here at some point to see if it's worth not uploading everything
	// each time
	Buffer *buf = dev->buffers->get(ResourceID{buffer.id});

	uint32_t count = u_end - u_start;
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf->id);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, u_start * sizeof(glm::mat4), count*sizeof(glm::mat4), matrices.data());

	u_start = UINT32_MAX;
	u_end = 0;

	return resized;
}

};
