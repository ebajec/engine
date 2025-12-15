#include "device_impl.h"
#include "resource_impl.h"

namespace ev2 {

//------------------------------------------------------------------------------
// Image assets

TextureAssetID load_texture_asset(Device *dev, const char *path)
{
	return EV2_NULL_HANDLE(TextureAsset);

}

void unload_texture_asset(Device *dev, const char *path)
{

}

TextureID get_texture_resource(Device *dev, TextureAssetID id)
{
	return EV2_NULL_HANDLE(Texture);
}

};
