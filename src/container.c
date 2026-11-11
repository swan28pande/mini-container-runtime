/* Runs inside the new namespaces: build the filesystem view, lock down, exec. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include "runtime.h"

#define DIE(msg) do { perror(msg); _exit(127); } while (0)

static void setup_dev(void)
{
    static const struct { const char *name; unsigned maj, min; } devs[] = {
        {"null", 1, 3}, {"zero", 1, 5}, {"full", 1, 7},
        {"random", 1, 8}, {"urandom", 1, 9}, {"tty", 5, 0},
    };
    char path[64];

    if (mount("tmpfs", "/dev", "tmpfs", MS_NOSUID | MS_STRICTATIME, "mode=755,size=64k") < 0)
        DIE("mount /dev");
    for (size_t i = 0; i < sizeof devs / sizeof devs[0]; i++) {
        snprintf(path, sizeof path, "/dev/%s", devs[i].name);
        if (mknod(path, S_IFCHR | 0666, makedev(devs[i].maj, devs[i].min)) < 0)
            DIE("mknod");
        chmod(path, 0666); /* mknod is subject to umask */
    }
    mkdir("/dev/pts", 0755);
    mkdir("/dev/shm", 01777);
    static const char *links[][2] = {
        {"/proc/self/fd", "/dev/fd"},       {"/proc/self/fd/0", "/dev/stdin"},
        {"/proc/self/fd/1", "/dev/stdout"}, {"/proc/self/fd/2", "/dev/stderr"},
    };
    for (size_t i = 0; i < sizeof links / sizeof links[0]; i++)
        if (symlink(links[i][0], links[i][1]) < 0)
            DIE("symlink");
}

static void setup_rootfs(struct container *c)
{
    /* Stop mount events propagating back to the host. */
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0)
        DIE("make / private");

    /* pivot_root needs the new root to be a mount point; the overlay is one,
     * but bind it onto itself so this also works for plain directories. */
    if (mount(c->rootfs, c->rootfs, NULL, MS_BIND | MS_REC, NULL) < 0)
        DIE("bind rootfs");
    if (chdir(c->rootfs) < 0)
        DIE("chdir rootfs");

    /* pivot_root(".", ".") stacks the old root on top of the new one;
     * detaching "." afterwards removes it without needing a put_old dir. */
    if (syscall(SYS_pivot_root, ".", ".") < 0)
        DIE("pivot_root");
    if (umount2(".", MNT_DETACH) < 0)
        DIE("detach old root");
    if (chdir("/") < 0)
        DIE("chdir /");

    mkdir("/proc", 0555);
    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0)
        DIE("mount /proc");
    mkdir("/sys", 0555);
    if (mount("sysfs", "/sys", "sysfs", MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0)
        DIE("mount /sys");
    mkdir("/sys/fs/cgroup", 0555); /* exists on sysfs already; harmless */
    mount("cgroup2", "/sys/fs/cgroup", "cgroup2", MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
    mkdir("/dev", 0755);
    setup_dev();
    mkdir("/tmp", 01777);
    if (mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777") < 0)
        DIE("mount /tmp");
}

int container_init(void *arg)
{
    struct container *c = arg;
    char go;

    /* Wait until the parent has moved us into the cgroup. */
    close(c->sync_pipe[1]);
    if (read(c->sync_pipe[0], &go, 1) != 1)
        _exit(127);
    close(c->sync_pipe[0]);

    if (sethostname(c->hostname, strlen(c->hostname)) < 0)
        DIE("sethostname");
    setup_rootfs(c);

    /* No privilege gain through setuid binaries, then filter syscalls.
     * seccomp goes last: the filter blocks mount() and friends. */
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0)
        DIE("no_new_privs");
    if (seccomp_install() < 0)
        DIE("seccomp");

    clearenv();
    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
    setenv("HOME", "/root", 1);
    setenv("TERM", "xterm", 1);

    execvp(c->argv[0], c->argv);
    fprintf(stderr, "minirun: exec %s: %s\n", c->argv[0], strerror(errno));
    _exit(127);
}
