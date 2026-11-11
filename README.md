# minirun: a minimal container runtime

A small container runtime in C that isolates a process using Linux namespaces, cgroups v2, an overlayfs root filesystem, and a seccomp filter. It is under 500 lines of code and needs nothing beyond libc and kernel headers.

**Stack:** C · Linux namespaces · cgroups v2 · overlayfs · seccomp-bpf

## What happens on `minirun run`

1. **Overlay rootfs.** The read-only image layers are mounted as overlayfs lowerdirs, with a fresh writable `upper/` under `/var/lib/minirun/<id>/`.
2. **cgroup.** `/sys/fs/cgroup/minirun-<id>` is created, and `memory.max`, `pids.max` and `cpu.max` are applied.
3. **Namespaces.** The process is `clone()`d into new PID, mount, UTS, IPC, network and cgroup namespaces. The child waits on a pipe until the parent has moved it into the cgroup.
4. **Filesystem view.** Mounts are made private, then `pivot_root` switches into the overlay. The runtime mounts `/proc`, a read-only `/sys`, the cgroup2 filesystem, a minimal tmpfs `/dev` (null, zero, random, urandom, tty) and `/tmp`.
5. **Lockdown.** `PR_SET_NO_NEW_PRIVS` is set, then a raw-BPF seccomp denylist is installed. Calls to mount, pivot_root, unshare, setns, module loading, kexec, reboot, ptrace, bpf and keyring syscalls fail with `EPERM`.
6. **exec.** The command runs with a clean environment. When it exits, the overlay mount and cgroup are removed. `upper/` is kept so the container's writes can be inspected.

## Build and run

You need Linux 5.x+ with cgroups v2 (the default on current distros), and the runtime must run as root.

```bash
make

# Get a root filesystem, e.g. Alpine's minirootfs
mkdir -p rootfs/alpine
curl -L https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-minirootfs-3.20.3-x86_64.tar.gz \
  | sudo tar -xz -C rootfs/alpine

sudo ./minirun run -m 256M -p 64 -c "50000 100000" -n box "$PWD/rootfs/alpine" -- /bin/sh
```

`LOWERDIRS` is colon-separated with the top layer first (overlayfs order), for example `"$PWD/layers/app:$PWD/layers/base"`.

| Flag | Meaning |
|---|---|
| `-m MEM` | `memory.max`, e.g. `256M` |
| `-p N` | `pids.max` |
| `-c "Q P"` | `cpu.max`: quota and period in µs (`50000 100000` = half a CPU) |
| `-n NAME` | container hostname |

## Not yet implemented

This is a skeleton. Next steps toward OCI compliance:

- Parse an OCI bundle's `config.json` instead of CLI flags
- User namespaces (rootless) and capability dropping
- Network setup (veth + bridge). The network namespace currently has only a loopback device, and it is down.
- `create` / `start` / `state` / `kill` / `delete` lifecycle commands and state files
- An allowlist seccomp profile instead of a denylist

## Layout

```
src/main.c       CLI, orchestration, clone + wait
src/overlay.c    overlayfs rootfs setup and teardown
src/cgroup.c     cgroups v2 limits
src/container.c  in-container setup: hostname, pivot_root, /proc /dev /sys, exec
src/seccomp.c    seccomp-bpf syscall denylist
```
