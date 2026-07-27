#ifndef EV2_ASSET_H
#define EV2_ASSET_H

#include "ev2/defines.h"
#include "ev2/context.h"
#include "ev2/resource.h"

namespace ev2 {

MAKE_ASSET_HANDLE(GfxPipeline);
MAKE_ASSET_HANDLE(ComputePipeline);
MAKE_ASSET_HANDLE(Shader);
MAKE_ASSET_HANDLE(ImageAsset);
MAKE_ASSET_HANDLE(Mesh);

ShaderID load_shader(GfxContext *ctx, const char *path);
void unload_shader(GfxContext *ctx, ShaderID id);

GfxPipelineID load_graphics_pipeline(GfxContext *ctx, const char *path);
void unload_graphics_pipeline(GfxContext *ctx, GfxPipelineID pipe);

ComputePipelineID load_compute_pipeline(GfxContext *ctx, const char *path);
void unload_compute_pipeline(GfxContext *ctx, ComputePipelineID pipe);

ImageAssetID load_image_asset(GfxContext *ctx, const char *path);
void unload_image_asset(GfxContext *ctx, ImageAssetID id);
ImageID get_image_resource(GfxContext *ctx, ImageAssetID id);

MeshID load_mesh(GfxContext *ctx, const char *path);
void unload_mesh(GfxContext *ctx, ShaderID id);
}

#endif
