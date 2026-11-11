/*
 * minirun - a minimal container runtime.
 *
 *   minirun run [-m MEM] [-p PIDS] [-c "QUOTA PERIOD"] [-n HOSTNAME] LOWERDIRS -- CMD [ARGS...]
 *
 * The parent sets up the overlay rootfs and cgroup, clones the child into new
 * namespaces, moves it into the cgroup, then signals it to continue.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "runtime.h"

#define STACK_SIZE (1024 * 1024)

int write_file(const char *path, const char *data)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    ssize_t len = (ssize_t)strlen(data);
    ssize_t n = write(fd, data, (size_t)len);
    int saved = errno;
    close(fd);
    errno = saved;
    return n == len ? 0 : -1;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s run [-m MEM] [-p PIDS] [-c \"QUOTA PERIOD\"] [-n HOSTNAME] "
            "LOWERDIRS -- CMD [ARGS...]\n"
            "  LOWERDIRS  colon-separated read-only layers, top layer first\n",
            prog);
    exit(2);
}

int main(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "run") != 0)
        usage(argv[0]);

    struct container c = {0};
    snprintf(c.id, sizeof c.id, "%lx%04x", (unsigned long)time(NULL), getpid() & 0xffff);
    snprintf(c.hostname, sizeof c.hostname, "minirun-%.8s", c.id);

    int opt;
    optind = 2;
    while ((opt = getopt(argc, argv, "+m:p:c:n:")) != -1) {
        switch (opt) {
        case 'm': snprintf(c.memory_max, sizeof c.memory_max, "%s", optarg); break;
        case 'p': c.pids_max = atoi(optarg); break;
        case 'c': snprintf(c.cpu_max, sizeof c.cpu_max, "%s", optarg); break;
        case 'n': snprintf(c.hostname, sizeof c.hostname, "%s", optarg); break;
        default: usage(argv[0]);
        }
    }
    if (optind >= argc)
        usage(argv[0]);
    c.lowerdirs = argv[optind++];
    if (optind < argc && strcmp(argv[optind], "--") == 0)
        optind++;
    if (optind >= argc)
        usage(argv[0]);
    c.argv = &argv[optind];

    if (geteuid() != 0) {
        fprintf(stderr, "minirun: must run as root\n");
        return 1;
    }

    int status = 1;
    if (overlay_mount(&c) < 0)
        goto out;
    if (cgroup_create(&c) < 0)
        goto out_cgroup;
    if (pipe2(c.sync_pipe, O_CLOEXEC) < 0) {
        perror("pipe2");
        goto out_cgroup;
    }

    char *stack = malloc(STACK_SIZE);
    if (!stack) {
        perror("malloc");
        goto out_cgroup;
    }
    int flags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC |
                CLONE_NEWNET | CLONE_NEWCGROUP | SIGCHLD;
    pid_t pid = clone(container_init, stack + STACK_SIZE, flags, &c);
    if (pid < 0) {
        perror("clone");
        free(stack);
        goto out_cgroup;
    }
    close(c.sync_pipe[0]);

    /* Place the child in its cgroup before it execs anything. */
    if (cgroup_add_pid(&c, pid) < 0) {
        kill(pid, SIGKILL);
    } else if (write(c.sync_pipe[1], "1", 1) != 1) {
        perror("sync write");
        kill(pid, SIGKILL);
    }
    close(c.sync_pipe[1]);

    int wstatus;
    while (waitpid(pid, &wstatus, 0) < 0 && errno == EINTR)
        ;
    status = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 128 + WTERMSIG(wstatus);
    free(stack);

out_cgroup:
    cgroup_cleanup(&c);
out_overlay:
    overlay_cleanup(&c);
out:
    return status;
}
