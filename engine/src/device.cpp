#include "ev2/device.h"

#include "device_impl.h"

#include "asset_table.h"
#include "pool.h"

#include "stb_image.h"

namespace ev2 {

Device *create_device(const char *path)
{
	stbi_set_flip_vertically_on_load(true);

	Device *dev = new Device{};

	dev->buffer_pool.reset(ResourcePool<Buffer>::create());
	dev->image_pool.reset(ResourcePool<Image>::create());
	dev->texture_pool.reset(ResourcePool<Texture>::create());

	GLint64 ubo_offset_alignment;
	glGetInteger64v(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_offset_alignment);

	dev->transforms = GPUTTable<glm::mat4>((size_t)ubo_offset_alignment);
	dev->view_data = GPUTTable<ViewData>((size_t)ubo_offset_alignment);

	dev->assets.reset(AssetTable::create(dev, path));

	return dev;
}

void destroy_device(Device *dev)
{
	AssetTable::destroy(dev->assets.get());

	dev->transforms.destroy(dev);
	dev->view_data.destroy(dev);

	delete dev;
}

};
