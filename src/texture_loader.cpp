#include "resource_loader.h"
#include "texture_loader.h"
#include "gl_debug.h"

#include <filesystem>

#include "stb_image.h"

namespace fs = std::filesystem;

static LoadResult gl_image_create(ResourceLoader *loader, void **res, void *info);
static void gl_image_destroy(ResourceLoader *loader, void *tex);

#if RESOURCE_ENABLE_HOT_RELOAD

struct ImageReloadInfo : public ResourceReloadInfo
{
	~ImageReloadInfo()
	{
		ImageFileCreateInfo *info = static_cast<ImageFileCreateInfo*>(p_create_info);
		delete info;
	}
};

static ResourceReloadInfo *create_image_reload_info(void *info) 
{
	ResourceReloadInfo *reload_info = new ResourceReloadInfo{};

	ImageFileCreateInfo *create_info = new ImageFileCreateInfo{};
	*create_info = *static_cast<ImageFileCreateInfo*>(info);

	reload_info->p_create_info = create_info;

	return reload_info;
}
#endif

ResourceFns g_image_loader_fns = {
	.create_fn = gl_image_create,
	.destroy_fn = gl_image_destroy,
	.create_reload_info_fn = create_image_reload_info
};

int GLImage::init()
{
	glGenTextures(1,&id);

	return 0;
}

LoadResult load_img_from_file(GLImage *tex, std::string_view path)
{
	int width, height, channels;
	uint8_t* rgba = stbi_load(path.data(),&width,&height,&channels,STBI_rgb_alpha);

	if (!rgba) {
		log_error("Failed to load image : %s",path.data());
		goto LOAD_TEX_FILE_ERROR_CLEANUP;
	}

	glBindTexture(GL_TEXTURE_2D,tex->id);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
	glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA, GL_UNSIGNED_BYTE, rgba);

	tex->fmt = TEX_FORMAT_RGBA8;
	tex->w = width;
	tex->h = height;
	tex->d = 1;

	if (uint32_t error = gl_check_err())
		goto LOAD_TEX_FILE_ERROR_CLEANUP;

	log_info("Loaded image file : %s",path.data());

	return RESULT_SUCCESS;

	LOAD_TEX_FILE_ERROR_CLEANUP:
	return RESULT_ERROR;
}

LoadResult gl_image_create(ResourceLoader *loader, void **res, void *info)
{
	TextureDesc *desc = static_cast<TextureDesc*>(info);
	std::unique_ptr<GLImage> tex (new GLImage{});

	tex->init();

	if (!fs::is_regular_file(desc->path)) {
		log_error("Image file does not exist : %s", desc->path.c_str());
		return RESULT_ERROR;
	}

	LoadResult result = load_img_from_file(tex.get(), desc->path);

	if (result != RESULT_SUCCESS)
		return result;

	*res = tex.release();
	return result;

}

void gl_image_destroy(ResourceLoader *loader, void *res)
{
	if (!res) return;

	GLImage *tex = static_cast<GLImage*>(res);

	if(tex->id) glDeleteTextures(1,&tex->id);
	delete tex;
}

Handle load_image_file(ResourceLoader *loader, std::string_view path)
{
	Handle h = loader->find(path);

	if (h)
		return h;

	h = loader->create_handle(RESOURCE_TYPE_IMAGE);

	TextureDesc desc = {
		.path = loader->make_path_abs(path.data())
	};

	LoadResult result = RESULT_SUCCESS;

	const ResourceEntry *ent = loader->get(h);

	if (!ent || ent->type != RESOURCE_TYPE_IMAGE) {
		goto error_cleanup;
	}

 	result = resource_load(loader, h, &desc);

	if (result != RESULT_SUCCESS)
		goto error_cleanup;

	loader->set_handle_key(h,path);

	return h;

error_cleanup:
	loader->destroy_handle(h);
	return 0;
}

const GLImage *get_image(ResourceLoader *loader, Handle h)
{
	const ResourceEntry *ent = loader->get(h);
	if (!ent || ent->type != RESOURCE_TYPE_IMAGE)
		return nullptr;

	return static_cast<const GLImage*>(ent->data);
}
