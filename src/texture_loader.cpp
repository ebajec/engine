#include "resource_loader.h"
#include "texture_loader.h"
#include "gl_debug.h"

#include <vector>
#include <filesystem>

#include "stb_image.h"

namespace fs = std::filesystem;

static LoadResult gl_image_create(ResourceLoader *loader, void **res, void *info);
static void gl_image_destroy(ResourceLoader *loader, void *tex);
static LoadResult gl_image_create_from_disk(ResourceLoader *loader, ResourceHandle h, const char *path); 

static LoadResult gl_image_upload_mem(ResourceLoader *loader, void *res, void *info);

ResourceFns g_image_alloc_fns = {
	.create = gl_image_create,
	.destroy = gl_image_destroy,
	.load_file = gl_image_create_from_disk
};

ResourceLoaderFns g_image_loader_fns {
	.loader_fn = gl_image_upload_mem,
	.post_load_fn = nullptr
};

static LoadResult gl_image_upload_mem(ResourceLoader *loader, void *res, void *info)
{
	GLImage *img = static_cast<GLImage*>(res);
	char *data = static_cast<char*>(info);

	if (!img || !data || !img->id) {
		return RESULT_ERROR;
	}

	glBindTexture(GL_TEXTURE_2D,img->id);
	glTexSubImage2D(GL_TEXTURE_2D,0,0,0, 
				 (int)img->w, (int)img->h, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glBindTexture(GL_TEXTURE_2D,0);
	
	if (gl_check_err()) {
		return RESULT_ERROR;
	}

	return RESULT_SUCCESS;
}

LoadResult gl_image_create(ResourceLoader *loader, void **res, void *info)
{
	ImageCreateInfo *ci = static_cast<ImageCreateInfo*>(info);

	std::unique_ptr<GLImage> img (new GLImage{});
	img->w = ci->w;
	img->h = ci->h;
	img->fmt = ci->fmt;
	img->d = 1;

	glGenTextures(1,&img->id);
	glBindTexture(GL_TEXTURE_2D,img->id);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, (int)ci->w, (int)ci->h);

	*res = img.release();

	return RESULT_SUCCESS;

}

void gl_image_destroy(ResourceLoader *loader, void *res)
{
	if (!res) return;

	GLImage *tex = static_cast<GLImage*>(res);

	if(tex->id) glDeleteTextures(1,&tex->id);
	delete tex;
}

static LoadResult gl_image_create_from_disk(ResourceLoader *loader, ResourceHandle h, const char *path) 
{
	int width, height, channels;
	int stbi_res = stbi_info(path, &width, &height, &channels);

	if (!stbi_res) {
		log_error("Failed to load image_file : %s",path);
		return RESULT_ERROR;
	}

	ImageCreateInfo img_info = {
		.w = (uint32_t)width,
		.h = (uint32_t)height,
		.fmt = TEX_FORMAT_RGBA8
	};

	LoadResult result = loader->allocate(h, &img_info);

	if (result != RESULT_SUCCESS) {
		return RESULT_ERROR;
	}

	uint8_t* rgba = stbi_load(path,&width,&height,&channels,STBI_rgb_alpha);
	result = loader->upload(h, RESOURCE_LOADER_IMAGE_MEMORY,rgba); 

	if (result != RESULT_SUCCESS) {
		return result;
	}

	return result;
}

ResourceHandle create_image_2d(ResourceLoader *loader, uint32_t width, uint32_t height, TexFormat fmt)
{
	ImageCreateInfo info = {
		.w = width,
		.h = height,
		.fmt = fmt
	};

	ResourceHandle h = loader->create_handle(RESOURCE_TYPE_IMAGE);
	LoadResult result = loader->allocate(h, &info); 

	if (result != RESULT_SUCCESS) {
		loader->destroy_handle(h);
		return RESOURCE_HANDLE_NULL;
	}

	return h;
}

ResourceHandle load_image_file(ResourceLoader *loader, std::string_view path)
{
	if (ResourceHandle h = loader->find(path)) {
		return h;
	}

	ResourceHandle h = loader->create_handle(RESOURCE_TYPE_IMAGE);

	LoadResult result = loader->load_file(h,path.data());

	if (result != RESULT_SUCCESS) {
		log_error("Failed to load image file : %s",path.data());
		goto load_failed;
	}

	loader->set_handle_key(h,path);

	log_info("Loaded image file : %s",path.data());
	return h;

load_failed:
	loader->destroy_handle(h);
	return h;
}

const GLImage *get_image(ResourceLoader *loader, ResourceHandle h)
{
	const ResourceEntry *ent = loader->get(h);
	if (!ent || ent->type != RESOURCE_TYPE_IMAGE)
		return nullptr;

	return static_cast<const GLImage*>(ent->data);
}
