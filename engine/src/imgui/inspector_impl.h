#ifdef EV2_ENABLE_IMGUI

#include <ev2/resource.h>

#include "utils/pool.h"

namespace ev2 {
	struct RenderGraphSubmission;
};

namespace ev2::imgui {

struct ImageCallback {
	void *usr;
	void (*callback)(void *, ImageID);

	void exec(ev2::ImageID image) {
		if (!callback)
			return;
		callback(usr, image);
	};
};

extern struct InspectorPanelState
{
	std::vector<ev2::ImageID> selected_images;

	PoolID selected;

	ImageCallback image_viewer_open;
	ImageCallback image_viewer_close;
} g_state;

extern void on_destroy_image(ev2::ImageID image);

extern void post_frame_submission_stats(const RenderGraphSubmission *submissions, uint32_t count);

}

#endif
