#ifndef EV2_INSPECTOR_H
#define EV2_INSPECTOR_H

#ifdef EV2_ENABLE_IMGUI

#include <ev2/resource.h>
#include <ev2/context.h>

#include <imgui.h>

namespace ev2::imgui 
{

constexpr const char *EDITOR_PANEL_NAME = "Editor";
constexpr const char *INSPECTOR_PANEL_NAME = "Inspector";

void inspector_panel_imgui(GfxContext *ctx);
void editor_panel_imgui(GfxContext *ctx);

void set_image_viewer_open_callback(void* usr, void (*callback)(void *, ev2::ImageID));
void set_image_viewer_close_callback(void* usr, void (*callback)(void *, ev2::ImageID));

}

#endif

#endif
