/*
 * seccomp-bpf denylist, written as raw BPF so there is no libseccomp dependency.
 * Blocked syscalls fail with EPERM; everything else is allowed.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

#include "runtime.h"

#if defined(__x86_64__)
#define AUDIT_ARCH_CURRENT AUDIT_ARCH_X86_64
#elif defined(__aarch64__)
#define AUDIT_ARCH_CURRENT AUDIT_ARCH_AARCH64
#else
#error "unsupported architecture"
#endif

#define DENY(nr) \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (nr), 0, 1), \
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA))

int seccomp_install(void)
{
    struct sock_filter filter[] = {
        /* Kill the process if it uses a different syscall ABI (e.g. x32/i386). */
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_CURRENT, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),

        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
#ifdef __x86_64__
        /* Reject x32 syscalls, which share the x86_64 arch value. */
        BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
#endif
        DENY(__NR_mount),
        DENY(__NR_umount2),
        DENY(__NR_pivot_root),
        DENY(__NR_unshare),
        DENY(__NR_setns),
        DENY(__NR_kexec_load),
        DENY(__NR_init_module),
        DENY(__NR_finit_module),
        DENY(__NR_delete_module),
        DENY(__NR_reboot),
        DENY(__NR_swapon),
        DENY(__NR_swapoff),
        DENY(__NR_ptrace),
        DENY(__NR_bpf),
        DENY(__NR_perf_event_open),
        DENY(__NR_keyctl),
        DENY(__NR_add_key),
        DENY(__NR_request_key),
        DENY(__NR_open_by_handle_at),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog prog = {
        .len = (unsigned short)(sizeof filter / sizeof filter[0]),
        .filter = filter,
    };
    return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog);
}
