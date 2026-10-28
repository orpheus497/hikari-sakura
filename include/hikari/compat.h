#if !defined(HIKARI_COMPAT_H)
#define HIKARI_COMPAT_H

/* [COMMENT] Script function and purpose: Supply, on Linux only, the two
base-system functions this tree calls that Linux does not reliably provide --
closefrom(2) and explicit_bzero(3).

On FreeBSD this header adds nothing: it pulls in the two base headers that
declare those functions and stops. No wrapper, no macro, no indirection -- the
call sites reach libc exactly as they did before this file existed, and the
preprocessed output of a FreeBSD build is unchanged. Linux pays for the branch.

Header-only, deliberately. This tree produces three binaries from three
separate link lines with three different flag sets -- the compositor with the
full wlroots/pango CFLAGS, hikari-topbar from the deliberately minimal
TOPBAR_CFLAGS snapshot, and hikari-unlocker from a rule of its own -- so a
compat.c would have to be compiled and threaded into each one that needed it,
three times, under three sets of flags. Nothing here allocates or touches stdio, because both
closefrom() call sites run between fork(2) and execl(3) in a child of a
multithreaded compositor, where malloc and stdio can deadlock on a lock another
thread held at fork time.

A build that has probed its libc may define HAVE_CLOSEFROM and/or
HAVE_EXPLICIT_BZERO; either one suppresses the matching shim and leaves the
libc symbol in place. That is the -DHAVE_* convention already used throughout
Makefile, and HAVE_EXPLICIT_BZERO is the spelling src/lock_mode.c already
anticipated. */

#if defined(__FreeBSD__)

/* [COMMENT] Action purpose: FreeBSD declares closefrom(2) in <unistd.h> and
explicit_bzero(3) in <strings.h>. Including them is the whole of this header on
FreeBSD, and makes it self-contained without interposing anything. */
#include <strings.h>
#include <unistd.h>

#elif defined(__linux__)

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

/* [COMMENT] Action purpose: Both shims below reach the kernel through
syscall(2) -- the closefrom() shim does; the explicit_bzero() one issues no
syscall at all and reaches memset through a volatile function pointer -- which
syscall(2) itself records as needing _DEFAULT_SOURCE on glibc
since 2.19. Nothing is defined here to arrange that: a feature-test macro set
in a header this deep is set after the C library's headers have already been
configured by the translation unit that included it, so it would be ignored
where it mattered and would silently change other declarations where it did
not. The compositor and hikari-topbar are both built without a strict -std, so
_DEFAULT_SOURCE is on by default. A build that adds -std=c11 or -std=c99 would
turn it off and lose syscall(2) along with it; -std=gnu11 is the spelling that
keeps it. */

#if !defined(HAVE_EXPLICIT_BZERO)

/* [COMMENT] Function purpose: explicit_bzero(3) for Linux C libraries that do
not export it. Scrubs `len` bytes at `buf` to zero in a way the compiler is not
permitted to discard. Used only on credential buffers. */
static inline void
hikari_compat_explicit_bzero(void *buf, size_t len)
{
  /* [COMMENT] Action purpose: A plain memset() here would be wrong, and not as
  a matter of style. Every caller of this function in the tree scrubs a
  password buffer and then returns, frees it, or execs -- nothing ever reads
  those bytes again. By the compiler's own escape and liveness analysis the
  store is therefore dead, and a dead store may legally be deleted outright.
  The cleartext password would stay in memory and the scrub would silently do
  nothing, which is worse than not scrubbing at all because the code claims
  otherwise.

  Calling through a volatile function pointer withdraws that licence. The
  pointer is a volatile object, so the compiler must re-read it from memory at
  the point of call and may not assume it still holds memset; it therefore
  cannot know what the callee writes or reads, and cannot prove the write is
  dead. The call must be emitted, and the stores it makes must stand. */
  void *(*volatile scrub)(void *, int, size_t) = memset;
  scrub(buf, 0, len);

  /* [COMMENT] Action purpose: Second barrier, where the compiler offers one.
  The empty asm is told it reads `buf` and clobbers memory, which forbids
  moving, merging or eliminating any store to that object across this point.
  It costs no instruction. Guarded because `__asm__` is a GNU C extension:
  without it the volatile call above still carries the guarantee. */
#if defined(__GNUC__)
  __asm__ __volatile__("" : : "r"(buf) : "memory");
#endif
}

#define explicit_bzero(buf, len) hikari_compat_explicit_bzero((buf), (len))

#endif /* !HAVE_EXPLICIT_BZERO */

#if !defined(HAVE_CLOSEFROM)

/* [COMMENT] Action purpose: The layout getdents(2) documents for
`struct linux_dirent64`, restated here because the walk below issues the
syscall directly. The libc wrappers are out of reach: glibc's getdents64()
needs _GNU_SOURCE, which this tree does not define, and relying on `struct
dirent` happening to match the kernel record is an assumption no man page
makes. The two wide members are spelled uint64_t and int64_t, which carry the
width and signedness of the ino64_t and loff_t the man page names; only
d_reclen and d_name are actually read. */
struct hikari_compat_dirent64 {
  uint64_t d_ino;
  int64_t d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[];
};

/* [COMMENT] Function purpose: Close every descriptor at or above `lowfd` by
walking /proc/self/fd. Second tier of hikari_compat_closefrom(): reached when
close_range(2) is unavailable or refused. Returns true when the walk ran to a
clean finish, false when /proc could not be read at all -- the caller then
falls through to a blind sweep. */
static inline bool
hikari_compat_closefrom_proc(int lowfd)
{
  /* [COMMENT] Action purpose: The naive form of this walk -- opendir() once,
  then close() each entry as readdir() yields it -- is silently broken, and it
  is broken in the direction that matters. Each close() removes an entry from
  the very directory being iterated, and readdir(3p) states that whether a
  subsequent call returns an entry for a file removed after the last
  opendir()/rewinddir() is unspecified. Directory reads are buffered in blocks,
  so entries can be skipped wholesale when the buffer refills from a listing
  that shifted underneath it. A skipped entry is a descriptor that survives
  into execl(3) -- precisely the leak these calls exist to prevent -- and it
  surfaces only at a particular buffer boundary, so it tests clean.

  The fix is to iterate to a fixed point: rescan from the top until a complete
  pass closes nothing. A pass that closes nothing did not mutate the directory,
  so its listing was stable and it saw every entry. Each pass that does close
  something strictly reduces the number of open descriptors, so the loop
  terminates. This needs no snapshot array, which matters in a post-fork()
  child where allocation is unsafe.

  The second hazard is self-inflicted: the descriptor opened to read the
  directory appears in the directory, and closing it mid-walk invalidates the
  walk. It is skipped by number and closed at the end of each pass. */
  bool progressed = true;

  while (progressed) {
    progressed = false;

    int dir_fd = open("/proc/self/fd", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd == -1) {
      return false;
    }

    /* [COMMENT] Action purpose: Typed as uint64_t so the buffer carries the
    alignment the first member of each returned record requires; the walk
    addresses it as bytes. */
    uint64_t aligned_buf[512];
    char *buf = (char *)aligned_buf;

    for (;;) {
      /* [COMMENT] Action purpose: syscall(2) is declared `long syscall(long
      number, ...)`, so every integer argument has to arrive as a long. An int
      passed through that ellipsis stays an int and leaves the rest of the
      register slot undefined on an LP64 ABI; the cast is what makes the
      descriptor number well defined on the way in. */
      long n = syscall(
          SYS_getdents64, (long)dir_fd, buf, (long)sizeof(aligned_buf));
      if (n <= 0) {
        break;
      }

      for (long off = 0; off < n;) {
        const struct hikari_compat_dirent64 *entry =
            (const struct hikari_compat_dirent64 *)(const void *)(buf + off);

        if (entry->d_reclen == 0) {
          break;
        }
        off += entry->d_reclen;

        /* [COMMENT] Action purpose: Entries are named by decimal descriptor
        number. Requiring the whole name to be digits rejects "." and ".."
        without a string compare, and refuses anything else outright rather
        than letting a partial parse close an unrelated descriptor. */
        unsigned long parsed = 0;
        const char *p = entry->d_name;
        if (*p < '0' || *p > '9') {
          continue;
        }
        for (; *p >= '0' && *p <= '9'; p++) {
          parsed = parsed * 10 + (unsigned long)(*p - '0');
          if (parsed > (unsigned long)INT_MAX) {
            break;
          }
        }
        if (*p != '\0' || parsed > (unsigned long)INT_MAX) {
          continue;
        }

        int fd = (int)parsed;
        if (fd < lowfd || fd == dir_fd) {
          continue;
        }
        if (close(fd) == 0) {
          progressed = true;
        }
      }
    }

    close(dir_fd);
  }

  return true;
}

/* [COMMENT] Function purpose: closefrom(2) for Linux. Closes every open
descriptor at or above `lowfd`, as FreeBSD's closefrom(2) does, and returns
void for the same reason FreeBSD's does: errors closing an individual
descriptor are ignored and there is nothing a caller could usefully do with a
status. Called between fork(2) and execl(3). */
static inline void
hikari_compat_closefrom(int lowfd)
{
  if (lowfd < 0) {
    lowfd = 0;
  }

#if defined(SYS_close_range)
  /* [COMMENT] Action purpose: close_range(2) does the whole job in one
  syscall, with no /proc dependency, and close_range(2) NOTES names that as the
  reason to prefer it over listing /proc/self/fd. The syscall is issued
  directly rather than through libc: the glibc wrapper arrived in 2.34 and
  needs _GNU_SOURCE, which this tree does not define, whereas the syscall
  itself is a kernel property (Linux 5.9) and is reachable from any C library.

  flags is 0 because closing outright is what both call sites need: each runs
  immediately before an execl(3), so there is nothing to defer. Of the defined
  flags, CLOSE_RANGE_CLOEXEC is documented on BOTH platforms but merely sets
  close-on-exec rather than closing, which is strictly weaker here;
  CLOSE_RANGE_UNSHARE is Linux-only, CLOSE_RANGE_CLOFORK FreeBSD-only, and
  UNSHARE's stated benefit is avoiding races with other threads sharing the
  descriptor table, which a just-fork()ed single-threaded child does not
  have. ~0U is the top of the range. Any failure, ENOSYS on a pre-5.9 kernel
  most likely, falls through to the next tier.

  Arguments are cast to long because syscall(2) is declared `long syscall(long
  number, ...)` and reads them at that width; the kernel takes the first two
  as unsigned int and truncates, so ~0U arrives as the intended ceiling. */
  if (syscall(SYS_close_range, (long)(unsigned int)lowfd, (long)~0U, 0L) == 0) {
    return;
  }
#endif

  if (hikari_compat_closefrom_proc(lowfd)) {
    return;
  }

  /* [COMMENT] Action purpose: Last resort, reached only when the kernel has no
  close_range(2) and /proc is not mounted or is restricted -- a container or
  chroot built that way. Sweep blindly to the descriptor ceiling. Most of these
  close() calls fail with EBADF, which is the cost of having no way to ask what
  is open. */
  long max_fd = sysconf(_SC_OPEN_MAX);
  if (max_fd < 0) {
    /* [COMMENT] Action purpose: sysconf() returning -1 without setting errno
    means the limit is indeterminate, so there is no bound to sweep to. 1024 is
    an arbitrary floor, not a documented value: it is chosen only so this tier
    does something rather than nothing in a case that already means both better
    mechanisms are missing. */
    max_fd = 1024;
  }
  for (long fd = lowfd; fd < max_fd; fd++) {
    close((int)fd);
  }
}

#define closefrom(lowfd) hikari_compat_closefrom(lowfd)

#endif /* !HAVE_CLOSEFROM */

#endif /* platform select */

#endif
