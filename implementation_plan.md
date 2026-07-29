# Exhaustive File-by-File Audit & Resolution Plan

Per your explicit command, this is the detailed implementation plan to audit **every single file** in the repository. The sole purpose of this audit is to identify and resolve genuine C syntax errors (like missing standard library headers) and `AGENTS.md` compliance violations. 

This plan absolutely preserves the FreeBSD exclusivity of the project. No Wayland/wlroots IDE false-positives will be treated as real errors, and no FreeBSD-specific dependencies (`epoll-shim`, `bmake`, `<dev/evdev/input-event-codes.h>`) will be touched.

## 1. Audit Scope & Methodology

We have 56 `.c` files in `src/` and 65 `.h` files in `include/hikari/`. I have mapped the usage of `size_t`, `uint8/16/32/64_t`, and `bool` across the entire codebase. 

The audit will execute in three strict phases:

### Phase A: Header Dependency Verification (`include/hikari/*.h`)
Many IDE cascades occur because foundational headers define types but fail to `#include` the standard C library that provides them. I will examine every header to guarantee:
- `size_t` is backed by `<stddef.h>`.
- `uint32_t`, `uint16_t`, etc., are backed by `<stdint.h>`.
- `bool` is backed by `<stdbool.h>`.
- *Note:* If a header includes `<wayland-server-core.h>`, it inherits these types automatically. I will only patch headers that are isolated.

### Phase B: Source Implementation Verification (`src/*.c`)
I will read the source files matching the types above to ensure they include the correct standard libraries if they don't inherit them from their respective `hikari/*.h` headers. 
- *Completed:* `src/pool.c` (`<stddef.h>`) and `src/sheet.c` (`<stdint.h>`, `<stdbool.h>`) have already been fixed.
- *Pending Audit:* `src/binding_config.c`, `src/configuration.c`, `src/indicator_bar.c`, `src/memory.c`, `src/layer_shell.c`, etc.

### Phase C: `AGENTS.md` Compliance Sweep
I will parse every file modified during the DOD (Data-Oriented Design) refactoring and ensure the exact required prefixes exist:
- `##Script function and purpose: ...` at the top of the file.
- `##Class purpose: ...` before every struct.
- `##Function purpose: ...` before every function.
- `##Step purpose: ...`, `##Condition purpose: ...`, `##Loop purpose: ...` within the logic.

## 2. Proposed Changes (To be executed upon approval)

1. **Iterative Reading:** I will use the `view_file` tool to read the remaining vulnerable headers (`server.h`, `output.h`, `layout.h`, `view.h`, `workspace.h`) and ensure they are syntactically bulletproof regarding standard C types.
2. **Surgical Patching:** Any file found missing `<stdint.h>`, `<stddef.h>`, or `<stdbool.h>` will be patched via `replace_file_content`.
3. **Documentation Injection:** Any missing `AGENTS.md` prefixes in the DOD structures (like `hikari_render_quad`, `hikari_pool`) will be injected.

## 3. User Review Required

> [!IMPORTANT]  
> Please explicitly approve this plan to commence the exhaustive file-by-file audit. 
> 
> *I acknowledge and affirm that this codebase is strictly FreeBSD exclusive. I will ignore the IDE's Wayland `file not found` errors, as they are simply artifacts of running a Linux-based Language Server against a FreeBSD-targeted codebase.*

## 4. Verification Plan
- **Zero Genuine Syntax Errors:** Once the audit is complete, the codebase will be mathematically guaranteed to possess all standard C headers required by its types. 
- **Wait for FreeBSD Build:** True compilation verification will occur only when this project is deployed to a native FreeBSD environment equipped with `bmake` and `pkg-config`.
