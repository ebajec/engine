#include <ev2/utils/ansi_colors.h>

#include "device_impl.h"
#include "resource_impl.h"

#include "stb_image.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace ev2 {

static ev2::Result create(Device *dev, ImageAsset * asset, const char *path);

static void destroy(Device *dev, void *usr)
{
	ImageAsset * asset = reinterpret_cast<ImageAsset*>(usr);

	if (asset->img.id) {
		ev2::destroy_image(dev, asset->img);
	}

	delete asset;
}

static ev2::Result reload(Device *dev, void **usr, const char *path)
{
	std::unique_ptr<ImageAsset> asset (new ImageAsset{});
	
	ev2::Result res = create(dev, asset.get(), path);

	if (res != ev2::SUCCESS)
		return res;

	ImageAsset *old = reinterpret_cast<ImageAsset*>(*usr);

	if (old)
		destroy(dev, old);

	*usr = asset.release();

	return res;
}

static void image_upload_gl(Device *dev, ImageID h, void *data, size_t size)
{
	Image *image = dev->image_pool->get(ResourceID{h.id});
	GLenum fmt, type;

	image_format_to_gl(image->fmt, &fmt, &type);

	glTextureSubImage2D(
		image->id, 
		0,
		0, 0, 
		image->w, image->h,
		fmt, type, data
	);
}

static ev2::Result create(Device *dev, ImageAsset * asset, const char *path)
{
	std::string syspath = 
		dev->assets->get_system_path(path);


	if (!fs::exists(syspath)) {
		log_error("Image does not exist : %s", path);
		return ev2::ELOAD_FAILED;
	}

	int width, height, channels;
	int stbi_res = stbi_info(syspath.c_str(), &width, &height, &channels);

	if (!stbi_res) {
		log_error("Failed to load image_file : %s",path);
		return ev2::ELOAD_FAILED;
	}

	uint8_t* rgba = stbi_load(syspath.c_str(),&width,&height,&channels,STBI_rgb_alpha);

	if (!rgba) {
		log_error("Failed to load image_file : %s",path);
		return ev2::ELOAD_FAILED;
	}

	ImageID img = create_image(dev, 
		(uint32_t)width, (uint32_t)height, 1, IMAGE_FORMAT_RGBA8);

	image_upload_gl(dev, img, rgba, sizeof(uint32_t)*width*height);

	asset->img = img;

	free(rgba);

	log_info(
		"Image: " COLORIZE_PATH(%s)"\n"
		"\tw=%d\n\th=%d\n"
		"\tchannels=%d"
		, path, width, height, channels);

	return ev2::SUCCESS;
}

//------------------------------------------------------------------------------
// Interface

ImageAssetID load_image_asset(Device *dev, const char *path)
{
	static AssetVTable vtbl = {
		.reload = reload,
		.destroy = destroy
	};

	std::unique_ptr<ImageAsset> asset (new ImageAsset{}); 
	ev2::Result res = create(dev, asset.get(), path);

	if (res != ev2::SUCCESS)
		return EV2_NULL_HANDLE(ImageAsset);

	AssetID id = dev->assets->allocate(&vtbl, asset.release(), path);

	return EV2_HANDLE_CAST(ImageAsset, id);
}

void unload_image_asset(Device *dev, ImageAssetID h)
{
}

ImageID get_image_resource(Device *dev, ImageAssetID h)
{
	ImageAsset *asset = dev->assets->get<ImageAsset>((uint32_t)h.id);
	return asset->img;
}

};
