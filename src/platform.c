// [COMMENT] Script function and purpose: Probe and report what this machine
// actually provides -- renderer buffer capabilities, which DRM node the
// renderer landed on, and whether XDG_RUNTIME_DIR can back client shm pools.
// Everything here is observed through public API at startup; nothing is
// inferred from the host operating system. See BLUEPRINT.md section 13.

#include <hikari/platform.h>

#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// [COMMENT] Action purpose: statfs(2) reports the filesystem type by name on
// FreeBSD (struct statfs.f_fstypename). Linux's struct statfs carries only a
// numeric f_type and lives in a different header; this file is only ever built
// on FreeBSD, so the guard exists so clangd-based IDE analysis on Linux keeps
// resolving the rest of the file rather than failing at the include. Mirrors
// the guard src/lock_mode.c already applies to explicit_bzero.
#if defined(__FreeBSD__)
#include <sys/mount.h>
#include <sys/param.h>
#define HIKARI_HAVE_STATFS_FSTYPENAME 1
#endif

#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/util/log.h>

struct hikari_platform hikari_platform;

/* [COMMENT] Function purpose: Resolve the renderer's DRM file descriptor back
to a device path, and count how many card nodes the machine exposes.

wlroots hands out the fd but never the path, and the path is the single most
useful fact for diagnosing a hybrid-graphics machine: it says which GPU the
renderer actually landed on. Resolution is by device number rather than by
guessing a name -- fstat() the fd, then stat() each /dev/dri entry looking for
a matching st_rdev. */
static void
probe_drm_node(struct hikari_platform *platform)
{
  snprintf(platform->drm_node, sizeof(platform->drm_node), "unknown");
  platform->drm_node_count = 0;

  DIR *dir = opendir("/dev/dri");
  if (dir == NULL) {
    return;
  }

  struct stat renderer_st;
  bool have_renderer_st =
      platform->drm_fd >= 0 && fstat(platform->drm_fd, &renderer_st) == 0;

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.') {
      continue;
    }

    // [COMMENT] Action purpose: Only card* nodes indicate a distinct GPU with
    // display capability. Counting renderD* as well would double every device
    // and turn a single-GPU machine into an apparent multi-GPU one.
    if (strncmp(entry->d_name, "card", 4) == 0) {
      platform->drm_node_count++;
    }

    if (!have_renderer_st) {
      continue;
    }

    char path[HIKARI_PLATFORM_PATH_MAX];
    if ((size_t)snprintf(path, sizeof(path), "/dev/dri/%s", entry->d_name) >=
        sizeof(path)) {
      continue;
    }

    struct stat entry_st;
    if (stat(path, &entry_st) == 0 && entry_st.st_rdev == renderer_st.st_rdev) {
      snprintf(platform->drm_node, sizeof(platform->drm_node), "%s", path);
    }
  }

  closedir(dir);
}

/* [COMMENT] Function purpose: Record the filesystem behind XDG_RUNTIME_DIR and
whether posix_fallocate() succeeds there.

This diagnoses client failures, not compositor ones. wlroots allocates its own
shared memory anonymously and is unaffected, but a Wayland client that places
its wl_shm pool in this directory will fail there on ZFS, which presents as
clients dying at startup with no compositor-side error. Probing by actually
calling posix_fallocate() rather than pattern-matching the filesystem name
keeps this true for any filesystem with the same limitation. */
static void
probe_runtime_dir(struct hikari_platform *platform)
{
  snprintf(platform->runtime_dir_fs, sizeof(platform->runtime_dir_fs),
      "unknown");
  platform->runtime_dir_fallocate = false;
  platform->runtime_dir_probed = false;

  const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
  if (runtime_dir == NULL || runtime_dir[0] == '\0') {
    return;
  }

#ifdef HIKARI_HAVE_STATFS_FSTYPENAME
  struct statfs fs;
  if (statfs(runtime_dir, &fs) == 0) {
    snprintf(platform->runtime_dir_fs, sizeof(platform->runtime_dir_fs), "%s",
        fs.f_fstypename);
  }
#endif

  char template[PATH_MAX];
  if ((size_t)snprintf(template, sizeof(template), "%s/hikari-probe.XXXXXX",
          runtime_dir) >= sizeof(template)) {
    return;
  }

  int fd = mkstemp(template);
  if (fd == -1) {
    return;
  }

  // [COMMENT] Action purpose: Unlink immediately so the probe leaves nothing
  // behind even if the process dies between here and the close below. The open
  // descriptor keeps the inode alive for the duration of the test.
  unlink(template);

  platform->runtime_dir_fallocate = posix_fallocate(fd, 0, 4096) == 0;
  platform->runtime_dir_probed = true;

  close(fd);
}

void
hikari_platform_probe(
    struct wlr_renderer *renderer, bool linux_dmabuf_available)
{
  struct hikari_platform *platform = &hikari_platform;

  memset(platform, 0, sizeof(*platform));

  platform->linux_dmabuf = linux_dmabuf_available;
  platform->drm_fd = -1;

  if (renderer != NULL) {
    platform->render_buffer_caps = renderer->render_buffer_caps;
    platform->drm_fd = wlr_renderer_get_drm_fd(renderer);
  }

  // [COMMENT] Action purpose: Decode the capability bitmask once, here, so no
  // caller has to remember which bit means what. can_render_to_data_ptr is what
  // decides whether an off-screen pass may target plain CPU memory (the pixman
  // renderer) or must go through a DMA-BUF (gles2, vulkan).
  platform->can_render_to_data_ptr =
      (platform->render_buffer_caps & WLR_BUFFER_CAP_DATA_PTR) != 0;
  platform->can_render_to_dmabuf =
      (platform->render_buffer_caps & WLR_BUFFER_CAP_DMABUF) != 0;

  probe_drm_node(platform);
  probe_runtime_dir(platform);
}

void
hikari_platform_log(void)
{
  const struct hikari_platform *platform = &hikari_platform;

  wlr_log(WLR_INFO, "platform: render buffer caps 0x%" PRIx32 " (%s%s%s)",
      platform->render_buffer_caps,
      platform->can_render_to_data_ptr ? "data-ptr " : "",
      platform->can_render_to_dmabuf ? "dmabuf " : "",
      (platform->render_buffer_caps & WLR_BUFFER_CAP_SHM) ? "shm" : "");

  // [COMMENT] Action purpose: Name the renderer only when the environment
  // forced one. wlroots exposes no renderer-name accessor, so anything else
  // would be a guess presented as a fact.
  const char *forced_renderer = getenv("WLR_RENDERER");
  if (forced_renderer != NULL) {
    wlr_log(WLR_INFO, "platform: WLR_RENDERER=%s", forced_renderer);
  }

  wlr_log(WLR_INFO, "platform: renderer DRM node %s (fd %d), %d card node%s "
      "present",
      platform->drm_node,
      platform->drm_fd,
      platform->drm_node_count,
      platform->drm_node_count == 1 ? "" : "s");

  /* [COMMENT] Action purpose: Multiple card nodes mean wlroots chose between
  GPUs, and a bad choice makes scanout fail on the connector attached to the
  other one -- the FB-3 hypothesis behind the long-standing eDP-1 failure.
  Naming the override here puts the fix in the log next to the symptom. */
  if (platform->drm_node_count > 1) {
    wlr_log(WLR_INFO,
        "platform: multiple GPUs present; if an output fails its scanout "
        "swapchain test, pin the device with WLR_DRM_DEVICES=/dev/dri/cardN "
        "(see BLUEPRINT.md section 13, FB-3)");
  }

  if (platform->runtime_dir_probed) {
    wlr_log(WLR_INFO,
        "platform: XDG_RUNTIME_DIR filesystem %s, posix_fallocate %s",
        platform->runtime_dir_fs,
        platform->runtime_dir_fallocate ? "supported" : "UNSUPPORTED");

    /* [COMMENT] Action purpose: This is a client-visible failure with no
    compositor-side symptom, so say so explicitly rather than leaving a bare
    "unsupported" for someone to interpret. linux-dmabuf is what lets GPU
    clients avoid the directory entirely, so its state decides how bad this is. */
    if (!platform->runtime_dir_fallocate) {
      wlr_log(WLR_ERROR,
          "platform: posix_fallocate is unsupported on XDG_RUNTIME_DIR (%s); "
          "clients backing wl_shm pools there will fail to start%s",
          platform->runtime_dir_fs,
          platform->linux_dmabuf
              ? ", though GPU clients can still negotiate via linux-dmabuf"
              : " and linux-dmabuf is NOT available to fall back on");
    }
  } else {
    wlr_log(WLR_INFO, "platform: XDG_RUNTIME_DIR not probed");
  }

  wlr_log(WLR_INFO, "platform: linux-dmabuf %s",
      platform->linux_dmabuf ? "available" : "unavailable");
}
