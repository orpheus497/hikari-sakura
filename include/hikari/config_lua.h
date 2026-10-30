/* Script function and purpose: The configuration reader's interface to Lua.
Declares the state lifecycle that reads one configuration file, the iterator and
the typed value readers every parser in this tree uses to get values out of the
table that file returns, and the single diagnostic sink those parsers report
through.

WHAT THIS IS NOT. It is not a configuration library. There is no path lookup, no
schema, no defaulting helper and no coercion policy: there is one declaration
below per shape the parsers need, so each has exactly one spelling.

THE LIFETIME RULE, UNCHANGED FROM LIBUCL. Nothing obtained from the
configuration outlives it, and it lives until hikari_config_lua_read() returns.
A const char * handed back by hikari_config_string() points into a Lua string
that the returned table keeps alive; Lua's collector does not move objects, so
the pointer stays valid across a collection, but lua_close() frees it. A caller
that needs the text afterwards copies it, exactly as it already does.

WHAT BOUNDS A CONFIGURATION, AND WHAT DOES NOT. hikari_config_lua_read() runs
the chunk under two limits: a 64 MiB allocator cap and a 10,000,000 VM
instruction count hook. Between them they stop the two shapes that occur in
practice -- `while true do end` is stopped by the hook, and
`string.rep("x", 1e9)` by the cap, which is what bounds the allocating cases:
that whole gigabyte is allocated inside one C call, during which the hook
counts nothing.

Neither bounds a single long-running C call, and that gap is reachable from the
whitelisted standard library. The hook counts VM instructions, and no VM
instruction is executed while a C function runs, so the hook cannot interrupt
one; a C function that does not allocate is invisible to the cap as well. Two
cases reach that gap: `string.rep("", math.maxinteger)` loops inside one C call
without the hook firing once and allocates nothing at all, so no cap can see
it; and Lua's pattern matcher backtracks, so `string.find` over a few dozen
characters with a dozen `.-` items runs for a long time in one C call, with
zero instructions counted and a few kilobytes allocated. Either line in a
configuration file hangs the compositor at startup, before any output is lit.

This is an accepted limitation, not an oversight. Closing it needs a
time-based watchdog rather than an instruction count, because a hook cannot
interrupt a C call either, so it would mean a signal or a thread -- a different
mechanism with its own hazards. A configuration file is code the user wrote and
runs as themselves; these limits exist to turn a mistake into a diagnostic, not
to sandbox a hostile author who already has their own shell. */

#if !defined(HIKARI_CONFIG_LUA_H)
#define HIKARI_CONFIG_LUA_H

#include <stdbool.h>
#include <stdint.h>

#include <lua.h>

/* Called with the table the configuration file returned, at absolute stack
index `root`. Returns the verdict hikari_config_lua_read() returns. The reader
runs inside a protected call and may leave values on the stack; it must not
write to the configuration table. */
typedef bool (*hikari_config_reader)(lua_State *L, int root, void *ud);

/* Function purpose: Read one configuration file. Builds a sandboxed Lua state,
evaluates `path` in it, and calls `reader` with the table the chunk returned.
The state is destroyed before this returns, so nothing the reader borrowed from
Lua may outlive it. Reports every failure itself and returns false. */
bool
hikari_config_lua_read(
    const char *path, hikari_config_reader reader, void *ud);

struct hikari_config_iter {
  int tidx;    /* absolute index of the table being walked; 0 once exhausted */
  int top;     /* lua_gettop() at init, restored by fini */
  int vidx;    /* absolute index of the current value, valid after a true next;
                  -1 while init is holding back a "that is not a table" report
                  that only next() has the section name to word */
  bool failed; /* the walk was abandoned, and the reason was reported */
};

/* Pushes the traversal cursor. Every exit from the loop must run
hikari_config_iter_fini(), which is the only thing that pops it. Handing this
anything but a table is not a programming error the caller has to rule out: it
is reported like any other malformed value, through iter->failed on the first
next(). */
void
hikari_config_iter_init(
    struct hikari_config_iter *iter, lua_State *L, int tidx);

/* On true: `*key` is the entry's key and the value sits at iter->vidx. On
false: the walk is over, and iter->failed distinguishes "finished" from
"malformed". `where` names the surrounding section in diagnostics. */
bool
hikari_config_iter_next(struct hikari_config_iter *iter,
    lua_State *L,
    const char **key,
    const char *where);

void
hikari_config_iter_fini(struct hikari_config_iter *iter, lua_State *L);

/* Function purpose: Push t[key] without consulting metatables and return its
type, LUA_TNIL when the key is absent. Always pushes exactly one value, which
the caller pops -- the one accessor here that does not leave the stack as it
found it.

When `tidx` does not hold a table the lookup is impossible: that is reported,
nil is pushed, and the return is LUA_TNONE. It is not LUA_TNIL, so that the
usual `!= LUA_TNIL` test treats the key as present and fails on the nil, rather
than reading it as absent and quietly applying a default. */
int
hikari_config_field(lua_State *L, int tidx, const char *key);

/* The four typed readers share one contract. On success they write `*out` and
return true. On a type mismatch, and identically on nil, they report through
hikari_config_error(), leave `*out` untouched and return false -- which is
ucl_object_to*_safe()'s contract exactly, so no call site changes shape. They
never raise, so a caller's `goto done` cleanup always runs. `where` and `key`
are used only in the message. The stack is left as it was found. */
bool
hikari_config_string(lua_State *L,
    int idx,
    const char *where,
    const char *key,
    const char **out);

bool
hikari_config_int(lua_State *L,
    int idx,
    const char *where,
    const char *key,
    int64_t *out);

bool
hikari_config_double(lua_State *L,
    int idx,
    const char *where,
    const char *key,
    double *out);

bool
hikari_config_bool(lua_State *L,
    int idx,
    const char *where,
    const char *key,
    bool *out);

/* Function purpose: The single sink for every configuration diagnostic.
Supplies the "configuration error:" prefix and the configuration file's full
path, which Lua's own messages truncate at LUA_IDSIZE and which a value error
has no other way to name. `where` is the surrounding section, or NULL. */
void
hikari_config_error(const char *where, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

#endif
