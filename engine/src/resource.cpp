#include "ev2/pipeline.h"

#include "device_impl.h"
#include "resource_impl.h"

#include "engine/resource/texture_loader.h"
#include "engine/resource/model_loader.h"
#include "engine/resource/buffer.h"

namespace ev2 {

BufferID create_buffer(Device *dev, size_t size, bool mapped)
{
	uint32_t flags = GL_DYNAMIC_STORAGE_BIT; 

	if (mapped)
		flags |= GL_MAP_WRITE_BIT | GL_MAP_COHERENT_BIT;

	::BufferID id = ::buffer_create(dev->rt.get(), size, flags);

	return id;
}

void destroy_buffer(Device *dev, BufferID buf)
{
	dev->rt->destroy_handle(buf);
}


//------------------------------------------------------------------------------

ImageID create_image(Device *dev, uint32_t w, uint32_t h, uint32_t d, ImageFormat fmt)
{
	return ::image_create_2d(dev->rt.get(), w, h);
}

ImageID load_image(Device *dev, const char *path)
{
	return ::image_load_file(dev->rt.get(), path); 
}

void destroy_image(Device *dev, ImageID img)
{
	dev->rt->destroy_handle(img);
}

TextureID create_texture(Device *dev, ImageID img, TextureFilter filter)
{
	Texture *tex = new Texture{};
	tex->filter = filter;
	tex->img = img;

	return EV2_HANDLE_CAST(Texture,tex);
}

void destroy_texture(Device *dev, TextureID id)
{
	Texture * tex = EV2_TYPE_PTR_CAST(Texture, id); 
	delete tex;
}

};
