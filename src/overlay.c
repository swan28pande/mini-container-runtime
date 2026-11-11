/* overlayfs rootfs: read-only lower layers + a per-container writable upper. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "runtime.h"

static int mkdir_p(const char *path, mode_t mode)
{
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) < 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    return (mkdir(tmp, mode) < 0 && errno != EEXIST) ? -1 : 0;
}

int overlay_mount(struct container *c)
{
    char upper[160], work[160], opts[4096];

    snprintf(c->state_dir, sizeof c->state_dir, MINIRUN_STATE_DIR "/%s", c->id);
    snprintf(c->rootfs, sizeof c->rootfs, "%s/merged", c->state_dir);
    snprintf(upper, sizeof upper, "%s/upper", c->state_dir);
    snprintf(work, sizeof work, "%s/work", c->state_dir);

    if (mkdir_p(upper, 0755) < 0 || mkdir_p(work, 0755) < 0 || mkdir_p(c->rootfs, 0755) < 0) {
        perror("overlay: mkdir");
        return -1;
    }

    int n = snprintf(opts, sizeof opts, "lowerdir=%s,upperdir=%s,workdir=%s",
                     c->lowerdirs, upper, work);
    if (n < 0 || (size_t)n >= sizeof opts) {
        fprintf(stderr, "overlay: mount options too long\n");
        return -1;
    }
    if (mount("overlay", c->rootfs, "overlay", 0, opts) < 0) {
        perror("overlay: mount");
        return -1;
    }
    return 0;
}

void overlay_cleanup(struct container *c)
{
    char path[300];

    if (c->rootfs[0] == '\0')
        return;
    if (umount2(c->rootfs, MNT_DETACH) < 0 && errno != EINVAL)
        perror("overlay: umount");
    /* Keep upper/ so a container's writes can be inspected; drop the rest. */
    rmdir(c->rootfs);
    snprintf(path, sizeof path, "%s/work/work", c->state_dir);
    rmdir(path);
    snprintf(path, sizeof path, "%s/work", c->state_dir);
    rmdir(path);
}
