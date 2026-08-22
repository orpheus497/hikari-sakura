#if !defined(HIKARI_PLATFORM_H)
#define HIKARI_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

struct wlr_renderer;

#define HIKARI_PLATFORM_PATH_MAX 64
#define HIKARI_PLATFORM_FSNAME_MAX 32

/* [COMMENT] Class purpose: What this compositor observed about the machine it
is actually running on, probed once at startup through public API rather than
assumed.

Three separate investigations (DECISIONS_LOG Phases 19, 33 and 53) each spent a
cycle re-deriving facts the compositor already knew at startup, and two of those
conclusions turned out to be wrong -- see BLUEPRINT.md section 13. This struct
exists so those facts are observed, logged, and available to code that has to
branch on them, instead of being folklore in a document. */
struct hikari_platform {
  /* Bitmask of enum wlr_buffer_cap describing what the renderer can use as a
  render target. This is the sanctioned way to ask whether off-screen rendering
  must go through a DMA-BUF or may target plain CPU memory; the two booleans
  below are the decoded answers callers actually branch on. */
  uint32_t render_buffer_caps;
  bool can_render_to_data_ptr;
  bool can_render_to_dmabuf;

  /* DRM node the renderer resolved to, or -1 and "unknown" when the renderer
  is not DRM-backed (the pixman software renderer, notably). `drm_node_count`
  counts /dev/dri/card* entries: more than one means the machine has multiple
  GPUs and wlroots had a choice to make, which is the FB-3 hypothesis. */
  int drm_fd;
  char drm_node[HIKARI_PLATFORM_PATH_MAX];
  int drm_node_count;

  /* XDG_RUNTIME_DIR filesystem type and whether posix_fallocate() works there.
  ZFS returns EOPNOTSUPP/EINVAL because copy-on-write cannot give POSIX
  pre-allocation guarantees, which breaks wl_shm pool allocation for clients
  that place their pools in this directory. The compositor itself is unaffected
  (wlroots uses anonymous POSIX shared memory), so this is reported to explain
  client failures, not compositor ones. See BLUEPRINT.md section 13, FB-1. */
  char runtime_dir_fs[HIKARI_PLATFORM_FSNAME_MAX];
  bool runtime_dir_fallocate;
  bool runtime_dir_probed;

  /* Whether linux-dmabuf-v1 was successfully advertised. When it was, GPU
  clients can negotiate buffers without touching the runtime directory at all,
  which is what makes an FB-1 filesystem survivable in practice. */
  bool linux_dmabuf;
};

extern struct hikari_platform hikari_platform;

/* [COMMENT] Function purpose: Fill in hikari_platform. Call once from
server_init(), after the renderer exists and after linux-dmabuf has been
created (or failed to be). Never fails: anything that cannot be determined is
left at its "unknown" default rather than aborting startup. */
void
hikari_platform_probe(
    struct wlr_renderer *renderer, bool linux_dmabuf_available);

/* [COMMENT] Function purpose: Emit the probe result as one contiguous block of
wlr_log(WLR_INFO) lines, so a bug report contains the platform facts without
the reporter having to know which six commands to run. */
void
hikari_platform_log(void);

#endif
