#ifndef MINIRUN_RUNTIME_H
#define MINIRUN_RUNTIME_H

#include <stddef.h>
#include <sys/types.h>

#define MINIRUN_STATE_DIR "/var/lib/minirun"
#define MINIRUN_CGROUP_ROOT "/sys/fs/cgroup"

struct container {
    char id[32];
    char hostname[64];
    const char *lowerdirs;   /* colon-separated, top layer first (overlayfs order) */
    char **argv;             /* command to run inside the container */

    /* resource limits; empty string / 0 means "leave unset" */
    char memory_max[32];     /* e.g. "256M" */
    int pids_max;
    char cpu_max[32];        /* e.g. "50000 100000" (quota period) */

    /* filled in during setup */
    char state_dir[128];
    char rootfs[160];        /* overlay merged dir */
    char cgroup_path[256];
    int sync_pipe[2];        /* parent -> child: "cgroup ready" */
};

/* overlay.c */
int overlay_mount(struct container *c);
void overlay_cleanup(struct container *c);

/* cgroup.c */
int cgroup_create(struct container *c);
int cgroup_add_pid(struct container *c, pid_t pid);
void cgroup_cleanup(struct container *c);

/* container.c: entry point of the cloned child */
int container_init(void *arg);

/* seccomp.c */
int seccomp_install(void);

/* util */
int write_file(const char *path, const char *data);

#endif
