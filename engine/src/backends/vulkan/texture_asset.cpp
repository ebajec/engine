#include <ev2/utils/ansi_colors.h>

#include "context.h"
#include "resource.h"

#include "stb_image.h"

namespace ev2 {

static ev2::Result create(GfxContext *ctx, ImageFile * asset, const char *path);

static void destroy(GfxContext *ctx, void *usr)
{
	ImageFile * asset = reinterpret_cast<ImageFile*>(usr);

	if (asset->img.id) {
		ev2::destroy_image(ctx, asset->img);
	}

	delete asset;
}

static ev2::Result reload(GfxContext *ctx, void **usr, const char *path)
{
	std::unique_ptr<ImageFile> asset (new ImageFile{});
	
	ev2::Result res = create(ctx, asset.get(), path);

	if (res != ev2::SUCCESS)
		return res;

	ImageFile *old = reinterpret_cast<ImageFile*>(*usr);

	if (old)
		destroy(ctx, old);

	*usr = asset.release();

	return res;
}

static ev2::Result create(GfxContext *ctx, ImageFile * asset, const char *path)
{
	std::vector<unsigned char> bytes;

	ev2::Result result = ctx->vfs->read_all(path, [&bytes](size_t size) -> unsigned char*{
		bytes.resize(size);
		return bytes.data();
	});

	int width, height, channels;
	int stbi_res = stbi_info_from_memory(bytes.data(), (size_t)bytes.size(), &width, &height, &channels);

	if (!stbi_res) {
		log_error("Failed to load image_file : %s",path);
		return ev2::ELOAD_FAILED;
	}

	uint8_t* rgba = stbi_load_from_memory(bytes.data(), (size_t)bytes.size(), &width,&height,&channels,STBI_rgb_alpha);

	if (!rgba) {
		log_error("Failed to load image_file : %s",path);
		return ev2::ELOAD_FAILED;
	}

	ImageID img = create_image(ctx, 
		(uint32_t)width, (uint32_t)height, 1, IMAGE_FORMAT_RGBA8, 
		ev2::IMAGE_USAGE_SAMPLED_BIT
	);

	// TODO : upload the data

	log_error("unimplemented");

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

ImageFileID load_image_asset(GfxContext *ctx, const char *path)
{
	static AssetVTable vtbl = {
		.reload = reload,
		.destroy = destroy
	};

	std::unique_ptr<ImageFile> asset (new ImageFile{}); 
	ev2::Result res = create(ctx, asset.get(), path);

	if (res != ev2::SUCCESS)
		return EV2_NULL_HANDLE(ImageFile);

	AssetID id = ctx->assets->allocate(&vtbl, asset.release(), path);

	return EV2_HANDLE_CAST(ImageFile, id);
}

void unload_image_asset(GfxContext *ctx, ImageFileID h)
{
}

ImageID get_image_resource(GfxContext *ctx, ImageFileID h)
{
	ImageFile *asset = ctx->assets->get<ImageFile>((uint32_t)h.id);
	return asset->img;
}

};
