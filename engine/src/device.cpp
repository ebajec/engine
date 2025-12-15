#include "ev2/device.h"

#include "device_impl.h"

#include "asset_table.h"
#include "pool.h"

namespace ev2 {

Device *create_device(const char *path)
{
	Device *dev = new Device{};

	dev->assets.reset(AssetTable::create(dev, path));

	dev->buffers.reset(ResourcePool<Buffer>::create());
	dev->images.reset(ResourcePool<Image>::create());
	dev->textures.reset(ResourcePool<Texture>::create());

	return dev;
}

void destroy_device(Device *dev)
{
	delete dev;
}
};
