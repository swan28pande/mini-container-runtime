/* cgroups v2: one child cgroup per container under the unified hierarchy. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "runtime.h"

static int set_limit(struct container *c, const char *file, const char *value)
{
    char path[320];
    snprintf(path, sizeof path, "%s/%s", c->cgroup_path, file);
    if (write_file(path, value) < 0) {
        fprintf(stderr, "cgroup: write %s=%s: %s\n", file, value, strerror(errno));
        return -1;
    }
    return 0;
}

int cgroup_create(struct container *c)
{
    char buf[32];

    snprintf(c->cgroup_path, sizeof c->cgroup_path, MINIRUN_CGROUP_ROOT "/minirun-%s", c->id);
    if (mkdir(c->cgroup_path, 0755) < 0) {
        perror("cgroup: mkdir (is cgroup v2 mounted at " MINIRUN_CGROUP_ROOT "?)");
        c->cgroup_path[0] = '\0';
        return -1;
    }

    /* Controllers must be enabled in the parent's subtree_control; most
     * systemd hosts already do this for the root cgroup. */
    if (c->memory_max[0] && set_limit(c, "memory.max", c->memory_max) < 0)
        return -1;
    if (c->pids_max > 0) {
        snprintf(buf, sizeof buf, "%d", c->pids_max);
        if (set_limit(c, "pids.max", buf) < 0)
            return -1;
    }
    if (c->cpu_max[0] && set_limit(c, "cpu.max", c->cpu_max) < 0)
        return -1;
    return 0;
}

int cgroup_add_pid(struct container *c, pid_t pid)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%d", (int)pid);
    return set_limit(c, "cgroup.procs", buf);
}

void cgroup_cleanup(struct container *c)
{
    if (c->cgroup_path[0] && rmdir(c->cgroup_path) < 0 && errno != ENOENT)
        perror("cgroup: rmdir");
}
