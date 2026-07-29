# Architectural and Structural Decisions Log

*Note: Most recent entries are listed at the top.*

---

## [2026-07-29 03:15] Decision: Data-Oriented Design (DOD) Orientation & FreeBSD Primary Target
* **Context:** The user requested modernizing the `hikari` Wayland compositor with primary focus on FreeBSD compatibility, thorough documentation inside `docs/`, and adoption of Data-Oriented Design (DOD) principles.
* **Decision:**
  1. Structure core data layouts (views, sheets, groups, outputs, tiles) into cache-aligned contiguous arrays / struct-of-arrays (SoA) where appropriate to minimize pointer chasing during render/layout loops.
  2. Isolate FreeBSD platform integration requirements (`evdev`, `epoll-shim`, `tmpfs` `/tmp` `posix_fallocate`, PAM unlocker, `seatd`) in system setup documentation (`docs/freebsd_setup.md`) and build definitions (`Makefile`).
  3. Strict adherence to `AGENTS.md` operational cycle: Ask → Explain → Justify → Wait for Approval → Execute.

---

## [2026-07-29 03:15] Decision: Devdocs Separation of Concerns
* **Context:** `AGENTS.md` mandates absolute separation of AI tracking docs (`.devdocs/`) from product documentation (`docs/`) and code in root.
* **Decision:** Keep all operational and tracking files inside `.devdocs/` and user/product technical documentation inside `docs/`.
