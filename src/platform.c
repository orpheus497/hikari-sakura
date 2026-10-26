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

/* [COMMENT] Action purpose: Linux has no filesystem-type name to read. Its
struct statfs, as statfs(2) documents it, carries no character member at all --
only a numeric f_type -- so the name has to come from somewhere else, and
/proc/self/mountinfo is where the kernel prints it as text.

The alternative is a table mapping f_type magic numbers to names, and it is
worse on both counts that matter here. It is a guess that rots: the table is
maintained by hand against a set statfs(2) itself describes as partly
"hardcoded in kernel sources", so a filesystem newer than the table reports as
unknown while looking authoritative. And it cannot answer at all in places
where the question is interesting: statfs(2) lists one value, 0xef53, for
EXT2_SUPER_MAGIC, EXT3_SUPER_MAGIC and EXT4_SUPER_MAGIC alike. A name the
kernel wrote is a fact about this mount; a magic number resolved through a
local table is this file's opinion about what that number used to mean. */
#if defined(__linux__)
#define HIKARI_HAVE_PROC_MOUNTINFO 1
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

#if defined(HIKARI_HAVE_PROC_MOUNTINFO)
/* [COMMENT] Function purpose: Linux counterpart to reading f_fstypename out of
struct statfs -- resolve the filesystem type name serving `path` and write it
into `out`. Leaves `out` untouched when the answer cannot be established, so
the caller's "unknown" default stands. */
static void
runtime_dir_fs_from_mountinfo(const char *path, char *out, size_t out_size)
{
  /* [COMMENT] Action purpose: Mount points are printed as absolute paths with
  no symlinks in them, so the path being matched has to be in the same form
  before a prefix comparison means anything. realpath() into a caller-owned
  buffer rather than letting it allocate: this is the only allocation the
  function would make, and it buys nothing. */
  char resolved[PATH_MAX];
  if (realpath(path, resolved) == NULL) {
    return;
  }

  FILE *mountinfo = fopen("/proc/self/mountinfo", "r");
  if (mountinfo == NULL) {
    return;
  }

  size_t best_len = 0;
  char line[8192];

  while (fgets(line, sizeof(line), mountinfo) != NULL) {
    /* [COMMENT] Action purpose: fgets() truncates a line longer than the
    buffer and would hand the remainder back as if it were a fresh record.
    Swallow the rest of an over-long line and skip it: a line this long is not
    one of the few that could plausibly serve XDG_RUNTIME_DIR, and skipping it
    costs a name while mis-parsing it would invent one. */
    if (strchr(line, '\n') == NULL && !feof(mountinfo)) {
      int c;
      while ((c = fgetc(mountinfo)) != '\n' && c != EOF) {
      }
      continue;
    }

    /* [COMMENT] Action purpose: proc_pid_mountinfo(5) fixes fields 1-6, then
    allows "zero or more" optional fields, then a single "-" field, then the
    filesystem type. Because that count is variable, the type cannot be reached
    by a fixed index from the start of the line -- the separator has to be
    found first, and only the token after it is the type. Field 5 is the mount
    point, which is at a fixed index and can be taken directly.

    The earliest the separator can appear is token 7 (six fixed fields and no
    optional ones), so the search starts there; neither field 6, a
    comma-separated option list, nor an optional field, which the page gives as
    "tag[:value]", can be a bare "-". */
    char *saved = NULL;
    char *token = strtok_r(line, " \t\n", &saved);
    const char *mount_point = NULL;
    const char *fs_type = NULL;
    int field = 0;

    while (token != NULL) {
      field++;
      if (field == 5) {
        mount_point = token;
      }
      if (field >= 7 && strcmp(token, "-") == 0) {
        fs_type = strtok_r(NULL, " \t\n", &saved);
        break;
      }
      token = strtok_r(NULL, " \t\n", &saved);
    }

    if (mount_point == NULL || fs_type == NULL) {
      continue;
    }

    size_t len = strlen(mount_point);
    if (len == 0 || strncmp(resolved, mount_point, len) != 0) {
      continue;
    }

    /* [COMMENT] Action purpose: A text prefix is not a path prefix. "/run/user"
    is a textual prefix of "/run/userdata" and has nothing to do with it, so the
    match only counts when the resolved path either ends at the mount point or
    continues with a separator. "/" is the one mount point that already ends in
    its own separator and would otherwise never match anything. */
    if (len > 1 && resolved[len] != '\0' && resolved[len] != '/') {
      continue;
    }

    /* [COMMENT] Action purpose: Longest match wins -- /run/user/1000 is served
    by the tmpfs mounted there, not by whatever carries /run or /. Equal-length
    matches mean several mounts stacked at one point; the later line is taken,
    but that is a tie-break and not a claim about which is visible, since
    proc_pid_mountinfo(5) does not say the list is ordered that way. */
    if (len < best_len) {
      continue;
    }
    best_len = len;

    /* [COMMENT] Action purpose: Copied verbatim, including any ".subtype"
    suffix, because the page gives the field as "type[.subtype]" and the
    subtype is part of what the kernel is reporting. Comparison above was
    literal text: a mount point the kernel prints in some other form than the
    resolved path simply fails to match and leaves the default standing, which
    is the safe direction -- a miss costs a diagnostic, a false match would
    print the wrong filesystem. */
    snprintf(out, out_size, "%s", fs_type);
  }

  fclose(mountinfo);
}
#endif

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

#if defined(HIKARI_HAVE_PROC_MOUNTINFO)
  runtime_dir_fs_from_mountinfo(
      runtime_dir, platform->runtime_dir_fs, sizeof(platform->runtime_dir_fs));
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
