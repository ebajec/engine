#ifndef EV2_RENDER_IMPL_H
#define EV2_RENDER_IMPL_H

#include <engine/renderer/opengl.h>

namespace ev2 {

struct RenderTarget
{
	uint32_t w;
	uint32_t h;

	GLuint fbo;
	GLuint color;
	GLuint depth;
};

};

#endif
