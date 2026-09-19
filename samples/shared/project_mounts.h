#ifndef EV2_PROJECT_MOUNTS_H
#define EV2_PROJECT_MOUNTS_H

#include <ev2/context.h>
#include <ev2/utils/log.h>

// @brief Mount the resource directories the calling executable registered with
// ev2_add_mount (cmake/ev2.cmake), which arrive as the EV2_PROJECT_MOUNTS macro.
//
// This lives in a header rather than in samples-shared because it has to be
// compiled as part of each executable to see that executable's EV2_PROJECT_MOUNTS.
// static gives each copy internal linkage, so copies compiled with different
// mount lists don't violate the one-definition rule.
//
// @return the number of mounts that failed (e.g. a name already mounted from
// EV2_MOUNTS, which takes precedence).
static inline int add_project_mounts(ev2::GfxContext *ctx)
{
	struct Mount
	{
		const char *name;
		const char *path;
	};

	int failures = 0;
#ifdef EV2_PROJECT_MOUNTS
	static constexpr Mount mounts[] = { EV2_PROJECT_MOUNTS };

	for (const Mount &mount : mounts) {
		if (ev2::add_mount(ctx, mount.name, mount.path) != ev2::SUCCESS) {
			log_warn("Failed to mount %s:// at %s", mount.name, mount.path);
			++failures;
		}
	}
#endif
	return failures;
}

#endif // EV2_PROJECT_MOUNTS_H
