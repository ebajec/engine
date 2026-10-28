#ifndef EV2_CORE_H
#define EV2_CORE_H

#define EV2_BACKEND_VULKAN
#define EV2_IMGUI_COMPATIBILITY

#define EV2_SHADER_BUILD_MOUNT_NAME "spirv"
#define EV2_CORE_MOUNT_NAME "core"

#include "ev2/defines.h"

#include <functional>

#ifdef EV2_BACKEND_VULKAN
#include <vulkan/vulkan_core.h>

#ifdef EV2_IMGUI_COMPATIBILITY
struct ImGui_ImplVulkan_InitInfo;
#endif
#endif

//------------------------------------------------------------------------------
//
//
// Context / Initialization`
//
//
//------------------------------------------------------------------------------

MAKE_HANDLE_VERSIONED(Buffer);
MAKE_HANDLE_VERSIONED(Image);
MAKE_HANDLE_VERSIONED(Texture);

MAKE_ASSET_HANDLE(GfxPipeline);
MAKE_ASSET_HANDLE(ComputePipeline);
MAKE_ASSET_HANDLE(Shader);
MAKE_ASSET_HANDLE(ImageAsset);
MAKE_ASSET_HANDLE(Mesh);

MAKE_HANDLE_VERSIONED(Bindings);
MAKE_HANDLE_VERSIONED(View);

enum {
	EV2_BASE_SET_BINDLESS, 
	EV2_BASE_SET_PER_FRAME,
	EV2_BASE_SET_PER_PASS,
	EV2_BASE_SET_COUNT
};

enum {
	EV2_GFX_SET_PER_DRAW = 3
};

namespace ev2 { 

enum Result
{
	SHOULD_CLOSE = 2,
	TIMEOUT = 1,
	SUCCESS = 0,
	ELOAD_FAILED = -1,
	ERESIZE_FAILED = -2,
	EINVALID_BINDING = -3,
	EINIT_FAILED = -4,
	EBAD_SHADER = -5,
	ERENDER_GRAPH = -6,
	EBAD_SWAPCHAIN = -7,
	EVULKAN = -8,
	EBAD_MOUNT = -9,
	EBAD_PATH = -10,
	EUNKNOWN = -1024
};

Result _set_error_internal(Result result, const char *file, int line, const char *msg, ...);
#define set_error(result, format, ...)\
	_set_error_internal(result, __FILE__, __LINE__, format, ##__VA_ARGS__)

struct GfxContext;

ev2::Result add_mount(GfxContext *ctx, const char *name, const char *path);
void destroy_context(GfxContext *ctx);
ev2::Result resize_swapchain(GfxContext *ctx, uint32_t width, uint32_t height);

#ifdef EV2_BACKEND_VULKAN

struct VulkanInitOptions
{
	const char ** validationLayers;
	size_t validationLayerCount;
	const char ** instanceExtensions;
	size_t instanceExtensionCount;

	bool enableValidationLayers = true;
	bool allowResourceInspection = false;
};

struct GfxContextVulkanInfo
{
	VkSurfaceKHR surface;
	uint32_t surface_width;
	uint32_t surface_height;
};

ev2::Result init_for_vulkan(const VulkanInitOptions &opts);
VkInstance get_vulkan_instance();
GfxContext *create_context_for_vulkan(const GfxContextVulkanInfo &params);

#ifdef EV2_IMGUI_COMPATIBILITY
void populate_imgui_vulkan_init_info(GfxContext *ctx, ImGui_ImplVulkan_InitInfo *info);
#endif


Result _set_error_vk_internal(VkResult result, const char *file, int line, const char *msg, ...);

#define check_vk_result(result, format, ...)\
	_set_error_vk_internal(result, __FILE__, __LINE__, format, ##__VA_ARGS__)

const VkPhysicalDeviceLimits *get_vulkan_physical_device_limits(GfxContext *ctx);

#endif

//------------------------------------------------------------------------------
//
//
// Resources
//
//
//------------------------------------------------------------------------------

enum ImageFormat
{
	IMAGE_FORMAT_RGBA8,
	IMAGE_FORMAT_RGBA32F,
	IMAGE_FORMAT_32F,
	IMAGE_FORMAT_R8_UNORM,
	IMAGE_FORMAT_R8_SNORM
};

enum ImageUsage
{
	// These are the vulkan flag bits
    IMAGE_USAGE_TRANSFER_SRC_BIT = 0x00000001,
    IMAGE_USAGE_TRANSFER_DST_BIT = 0x00000002,
    IMAGE_USAGE_SAMPLED_BIT = 0x00000004,
    IMAGE_USAGE_STORAGE_BIT = 0x00000008,
    IMAGE_USAGE_COLOR_ATTACHMENT_BIT = 0x00000010,
    IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT = 0x00000020,
    IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT = 0x00000040,
    IMAGE_USAGE_INPUT_ATTACHMENT_BIT = 0x00000080,
    IMAGE_USAGE_HOST_TRANSFER_BIT = 0x00400000,
};
typedef uint32_t ImageUsageFlags;

enum TextureFilter
{
	FILTER_NEAREST,
	FILTER_BILINEAR,
};

enum BufferUsageFlagBits
{
	// These are the vulkan flag bits
    BUFFER_USAGE_TRANSFER_SRC_BIT = 0x00000001,
    BUFFER_USAGE_TRANSFER_DST_BIT = 0x00000002,
    BUFFER_USAGE_UNIFORM_BUFFER_BIT = 0x00000010,
    BUFFER_USAGE_STORAGE_BUFFER_BIT = 0x00000020,
    BUFFER_USAGE_INDEX_BUFFER_BIT = 0x00000040,
    BUFFER_USAGE_VERTEX_BUFFER_BIT = 0x00000080,
    BUFFER_USAGE_INDIRECT_BUFFER_BIT = 0x00000100,
};

typedef uint32_t BufferUsageFlags;

struct ImageUpload
{
	size_t src_offset;
	uint32_t x, y, z;
	uint32_t w, h, d{1};
	uint32_t level;
};

struct BufferUpload
{
	size_t src_offset = 0;
	size_t dst_offset = 0;
	size_t size;
};

struct UploadContext
{
	void *ptr;
	size_t size;
	uint32_t allocation_index;
};

UploadContext begin_upload(GfxContext *ctx, size_t bytes, size_t align);
void flush_uploads(GfxContext * ctx);

// @brief Schedule uploads to be executed on next flush of the corresponding
// upload pool.
//
// @return Counter value reached by pool upon completion of the upload.
uint64_t commit_buffer_uploads(GfxContext *ctx, UploadContext uc, BufferID buf, 
							   const BufferUpload *uploads, uint32_t count);
uint64_t commit_buffer_uploads(GfxContext *ctx, UploadContext uc, BufferID buf, 
							   BufferUpload up);
uint64_t commit_image_uploads(GfxContext *ctx, UploadContext uc, ImageID img, 
							  const ImageUpload *uploads, uint32_t count);

//--------------------------------------------------------------------
// Buffer

BufferID create_buffer(GfxContext *ctx, size_t size, BufferUsageFlags usage, 
					   size_t align = 0);
void destroy_buffer(GfxContext *ctx, BufferID buf);
VkDeviceAddress get_buffer_device_address(GfxContext *ctx, BufferID h);

//--------------------------------------------------------------------
// Image

ImageID create_image(GfxContext *ctx, uint32_t w, uint32_t h, uint32_t d, 
					 ImageFormat fmt, ImageUsageFlags usage, uint32_t levels = 1);

/// @brief Queue this resource for deletion after it is no longer in
/// use by the GPU
void destroy_image(GfxContext *ctx, ImageID img);

/// @brief Callback to be executed before destroying this resource 
void pre_destroy_callback(GfxContext *ctx, ImageID img, std::function<void()> &&callback);

void get_image_dims(GfxContext *ctx, ImageID h_img, 
					uint32_t *w, uint32_t *h, uint32_t*d, uint32_t *levels = nullptr);

/// @brief Debug helper
void set_image_name(GfxContext *ctx, ImageID img, const char *name);
const char *get_image_name(GfxContext *ctx, ImageID img);

//--------------------------------------------------------------------
// Texture

TextureID create_texture(GfxContext *ctx, ImageID img, TextureFilter filter,
						 uint32_t level = 0, uint32_t layer = 0);
void destroy_texture(GfxContext *ctx, TextureID tex);
uint32_t get_bindless_handle(GfxContext *ctx, TextureID h);
ImageID get_backing_image(GfxContext *ctx, TextureID h);
void get_texture_gpu_handle(GfxContext *ctx, TextureID h, VkImageView *view);
void get_texture_dims(GfxContext *ctx, TextureID tex, uint32_t *w, uint32_t *h, uint32_t*d);

//------------------------------------------------------------------------------
//
//
// Assets
//
//
//------------------------------------------------------------------------------

ShaderID load_shader(GfxContext *ctx, const char *path);
void unload_shader(GfxContext *ctx, ShaderID id);

GfxPipelineID load_graphics_pipeline(GfxContext *ctx, const char *path);
void unload_graphics_pipeline(GfxContext *ctx, GfxPipelineID pipe);

ComputePipelineID load_compute_pipeline(GfxContext *ctx, const char *path);
ComputePipelineID load_compute_pipeline(GfxContext *ctx, const char *path, const char *entrypoint);
void unload_compute_pipeline(GfxContext *ctx, ComputePipelineID pipe);

ImageAssetID load_image_asset(GfxContext *ctx, const char *path);
void unload_image_asset(GfxContext *ctx, ImageAssetID id);
ImageID get_image_resource(GfxContext *ctx, ImageAssetID id);

MeshID load_mesh(GfxContext *ctx, const char *path);
MeshID load_mesh_from_memory(GfxContext *ctx, const char *path);
void unload_mesh(GfxContext *ctx, ShaderID id);

//------------------------------------------------------------------------------
//
//
// Pipeline
//
//
//------------------------------------------------------------------------------

constexpr uint32_t EV2_MAX_BINDLESS_DESCRIPTORS = 1024;

enum IndexType
{
	INDEX_TYPE_UINT32,
	INDEX_TYPE_UINT16,
	INDEX_TYPE_UINT8,
};

enum Usage
{
    USAGE_UNDEFINED,
    USAGE_TRANSFER_SRC,
    USAGE_TRANSFER_DST,
    USAGE_SAMPLED_GRAPHICS,
    USAGE_UNIFORM_READ,
    USAGE_COLOR_ATTACHMENT,
    USAGE_DEPTH_ATTACHMENT,
    USAGE_SAMPLED_COMPUTE,
    USAGE_STORAGE_READ_GRAPHICS,
    USAGE_STORAGE_READ_COMPUTE,
    USAGE_STORAGE_WRITE_COMPUTE,
    USAGE_STORAGE_READ_WRITE_COMPUTE,
    USAGE_INDEX_INPUT,
    USAGE_VERTEX_INPUT,
    USAGE_MAX_ENUM,
};

enum BindingMode {
	BINDING_MODE_STATIC,
	BINDING_MODE_DYNAMIC
};

BindingsID create_bindings(GfxContext *ctx, GfxPipelineID pipeline_id, 
								 uint32_t index, BindingMode mode);
BindingsID create_bindings(GfxContext *ctx, ComputePipelineID pipeline_id, 
								 uint32_t index, BindingMode mode);

void destroy_bindings(GfxContext *ctx, BindingsID bindings);

Result reset_bindings(GfxContext *ctx, BindingsID bindings);
void flush_bindings(GfxContext *ctx, BindingsID bindings_id);

/// @brief Bind a buffer to the binding given by name.
/// The binding is assumed to not be an array type.
Result bind_buffer(
	GfxContext *ctx, 
	BindingsID binding_handle, 
	const char *name,
	BufferID buffer_handle, 
	size_t offset, 
	size_t size
);

Result bind_buffer_indexed(
	GfxContext *ctx, 
	BindingsID id, 
	const char *name,
	uint32_t dst_index,
	BufferID buffer_handle, 
	size_t offset, 
	size_t size
);

/// @brief Bind a texture to the binding given by name.
/// The binding is assumed to not be an array type.
Result bind_texture(
	GfxContext *ctx, 
	BindingsID binding_handle,
	const char *name,
	TextureID texture_handle  
); 

Result bind_texture_indexed(
	GfxContext *ctx, 
	BindingsID binding_handle,
	const char *name,
	uint32_t dst_index,
	TextureID texture_handle  
); 

/// @brief Bind a single mip level + layer of an image to the binding
/// given by name.  The binding is assumed to not be an array type.
Result bind_image(
	GfxContext *ctx,
	BindingsID binding_handle,
	const char *name,
	ImageID image_handle,
	uint32_t level = 0,
	uint32_t layer = 0
);

Result bind_image_indexed(
	GfxContext *ctx,
	BindingsID binding_handle,
	const char *name,
	uint32_t dst_index,
	ImageID image_handle,
	uint32_t level = 0,
	uint32_t layer = 0
);

//------------------------------------------------------------------------------
// Rendering

MAKE_HANDLE(Pass);
MAKE_HANDLE(RenderTarget);

struct DrawCommand 
{
    unsigned int  count;
    unsigned int  instanceCount;
    unsigned int  firstIndex;
    int  baseVertex;
    unsigned int  baseInstance;
};

enum RenderTargetFlagBits
{
	// Create a new color image for this target 
	RENDER_TARGET_CREATE_COLOR_BIT = 0x2,
	// Create a new depth buffer for this target
	RENDER_TARGET_CREATE_DEPTH_BIT = 0x4,
	// Create a new depth buffer for this target
	RENDER_TARGET_CREATE_STENCIL_BIT = 0x8,
};
typedef uint32_t RenderTargetFlags;

RenderTargetID create_render_target(
	GfxContext *ctx, 
	uint32_t w, 
	uint32_t h, 
	RenderTargetFlags flags
);
void destroy_render_target(
	GfxContext *ctx, 
	RenderTargetID id
);

VkImageView get_render_target_color_view(RenderTargetID target);
ImageID get_render_target_color_image(RenderTargetID target);

void get_render_target_views(RenderTargetID handle, 
							  VkImageView* color, VkImageView* depth);
void get_render_target_images(RenderTargetID target,
							  ImageID *color, ImageID *depth);

Result begin_frame(GfxContext *ctx);
Result end_frame(GfxContext *ctx);

ViewID create_view(GfxContext *ctx, float view[], float proj[]);
void update_view(GfxContext *ctx, ViewID handle, float view[], float proj[]);
void destroy_view(GfxContext *ctx, ViewID handle);

struct Rect
{
	uint32_t x0, y0;
	uint32_t w, h;
};

constexpr Rect WHOLE_IMAGE = Rect{0, 0, UINT32_MAX, UINT32_MAX};

struct GfxPassInfo
{
	RenderTargetID target;
	ViewID view;
	Rect viewport = WHOLE_IMAGE;
	Rect scissor = WHOLE_IMAGE;

	bool clear_color : 1 = true;
	bool clear_depth : 1 = true;

	const char *name = nullptr;
};

PassID begin_gfx_pass(GfxContext *ctx, const GfxPassInfo *info);

// @brief Begin a render pass.  Configures bindings for pass-specific data  
// (view matrices, etc).    
PassID begin_gfx_pass(
	GfxContext *ctx, 
	RenderTargetID target, ViewID view,
	Rect viewport = WHOLE_IMAGE, Rect scissor = WHOLE_IMAGE
);

// @brief Begin a compute pass.  Does not bind any descriptor sets.   
PassID begin_compute_pass(
	GfxContext *ctx, const char *name = nullptr
);

// @brief End a pass.  The ordering of this call determines the read/write
// dependencies between passes.
//
// E.g., if Pass A writes to X and is submitted first, and Pass B reads X, 
// and is submitted after, a dependency will be inserted between A and B
void end_pass(GfxContext *ctx, PassID pass);

//------------------------------------------------------------------------------
// command recording

void cmd_bind_resources(PassID pass_id, BindingsID bindings_id);
void cmd_bind_compute_pipeline(PassID pass_id, ComputePipelineID pipeline_id);
void cmd_bind_gfx_pipeline(PassID pass_id, GfxPipelineID pipeline_id);
void cmd_bind_index_buffer(PassID pass_id, BufferID buf, size_t offset);
void cmd_bind_vertex_buffer(PassID pass_id, BufferID buf, size_t offset);
void cmd_dispatch(PassID pass_id, uint32_t countx, uint32_t county, uint32_t countz);
void cmd_draw_indirect(PassID pass_id, BufferID buf, size_t offset, uint32_t count, uint32_t stride); 
void cmd_use_buffer(PassID pass_id, BufferID buf_id, Usage usage);
void cmd_use_image(PassID pass_id, ImageID img_id, Usage usage);
void cmd_push_constant(PassID pass_id, GfxPipelineID pipeline_id, 
					   uint32_t offset, uint32_t size, void *data);
void cmd_push_constant(PassID pass_id, ComputePipelineID pipeline_id, 
					   uint32_t offset, uint32_t size, void *data);
void cmd_custom(PassID pass_id, std::function<void(VkCommandBuffer)>&& callback);
};

#endif // EV2_CORE_H
