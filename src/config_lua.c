/* Script function and purpose: Reads one Lua configuration file for hikari, and
supplies the accessors every parser in this tree uses to take values out of it.
This file is the whole of the replacement for libucl's parser: the state it
builds, the sandbox that state runs the configuration in, and the ten functions
declared in <hikari/config_lua.h>.

THE ONE INVARIANT THAT MATTERS. lua_tolstring() REWRITES the stack slot it is
given when that slot is not already a string, because it has to materialise a
buffer the caller can keep. Doing that to the key slot of a lua_next() walk
makes the next lua_next() call raise "invalid key to 'next'", and doing it to a
value slot destroys the integer subtype a later test depends on. So:
lua_tolstring and lua_tostring are called in exactly eight places in this file,
and every one of them sits directly behind an exact `lua_type(...) ==
LUA_TSTRING` test. No slot this file touches can therefore ever change type,
whatever index a caller hands in, and the hazard is structurally absent rather
than merely avoided. Any new call that is not behind that test is a defect.

THE SECOND INVARIANT. A Lua error is a longjmp. It runs no cleanup, so it skips
every free() a C frame still owed, and if no protected call is below it, it
reaches the panic function and abort()s -- a typo in a user's configuration
would otherwise take the session down with SIGABRT. Everything this file does to
a lua_State after the state exists therefore runs inside a lua_pcall: the
library and sandbox setup, the configuration chunk, and the caller's reader.
Nothing here holds heap memory across a call that can raise; what the reader
owns is the reader's problem, and it is why the accessors below report and
return false instead of raising. */

#include <hikari/config_lua.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <lauxlib.h>
#include <lualib.h>

/* Four orders of magnitude above any real configuration, and the only thing
that stops string.rep("x", 1e9): the instruction hook cannot see an allocation.
*/
#define CONFIG_MEMORY_CAP ((size_t)64 * 1024 * 1024)

/* A configuration that builds 200 bindings in a loop uses under 100k VM
instructions, so this is ~100x headroom and still stops `while true do end`. */
#define CONFIG_INSTRUCTION_BUDGET 10000000

#define CONFIG_INCLUDE_DEPTH_MAX 8

/* The path of the configuration being read, for hikari_config_error(). Lua
truncates the file name in its own messages at LUA_IDSIZE (60), so a value error
-- which has no line to report anyway -- prints the path from here instead. */
static const char *config_path = NULL;

/* Address-as-registry-key: the sandbox environment is stored under
&config_sandbox_key so include() can give an included chunk the same _ENV, with
no string key to collide with anything else in the registry. */
static const char config_sandbox_key = 0;

struct config_budget {
  size_t used;
  size_t cap;
};

struct config_ctx {
  const char *path;
  hikari_config_reader reader;
  void *ud;
  bool verdict;
};

void
hikari_config_error(const char *where, const char *fmt, ...)
{
  va_list ap;

  fputs("configuration error: ", stderr);

  if (config_path != NULL) {
    fprintf(stderr, "%s: ", config_path);
  }

  if (where != NULL) {
    fprintf(stderr, "%s: ", where);
  }

  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);

  fputc('\n', stderr);
}

/* Function purpose: Whether Lua already named the configuration file in this
message, so that the path is not printed twice.

Action purpose: Lua prefixes its load and run messages with the chunk name and
truncates it at LUA_IDSIZE by keeping the TAIL -- "...gggg/hikari.lua:3:" -- so
the base name always survives a truncation. Its absence therefore means Lua
built this message without a chunk name at all, which the refusal of a
precompiled chunk does, and that message needs our path in front of it. */
static bool
config_message_names_file(const char *message)
{
  const char *base;

  if (config_path == NULL) {
    return true;
  }

  base = strrchr(config_path, '/');
  base = (base == NULL) ? config_path : base + 1;

  return base[0] != '\0' && strstr(message, base) != NULL;
}

/* Function purpose: Report a failed load or protected call, whose error object
Lua left on the stack top.

Action purpose: the message is printed from the slot that owns it and the stack
is then cleared, so nothing is copied and nothing outlives its owner. A chunk
may raise any value at all, so a non-string object is described rather than
converted: lua_tolstring() would rewrite the slot, and luaL_tolstring() would
honour a __tostring metamethod that can itself raise. */
static void
config_report(lua_State *L, int status)
{
  if (status == LUA_ERRMEM) {
    hikari_config_error(NULL,
        "out of memory -- a configuration may not use more than %zu bytes",
        CONFIG_MEMORY_CAP);
  } else if (lua_type(L, -1) == LUA_TSTRING) {
    const char *message = lua_tostring(L, -1);

    if (config_message_names_file(message)) {
      fprintf(stderr, "configuration error: %s\n", message);
    } else {
      hikari_config_error(NULL, "%s", message);
    }
  } else {
    fprintf(stderr, "configuration error: (error object is a %s value)\n",
        lua_typename(L, lua_type(L, -1)));
  }

  lua_settop(L, 0);
}

static void
config_type_error(lua_State *L,
    int idx,
    const char *where,
    const char *key,
    const char *expected)
{
  hikari_config_error(where, "\"%s\" expects %s, got %s", key, expected,
      lua_typename(L, lua_type(L, idx)));
}

bool
hikari_config_string(lua_State *L,
    int idx,
    const char *where,
    const char *key,
    const char **out)
{
  int i = lua_absindex(L, idx);
  const char *value;
  size_t len;

  if (lua_type(L, i) != LUA_TSTRING) {
    config_type_error(L, i, where, key, "string");
    return false;
  }

  value = lua_tolstring(L, i, &len);

  /* Action purpose: a Lua string may contain NUL bytes, and every consumer of
  this pointer treats it as a C string. Truncating silently at the first NUL is
  the ambiguity libucl also had; naming it costs three lines. */
  if (strlen(value) != len) {
    hikari_config_error(where, "\"%s\" contains an embedded NUL byte", key);
    return false;
  }

  *out = value;

  return true;
}

bool
hikari_config_int(lua_State *L,
    int idx,
    const char *where,
    const char *key,
    int64_t *out)
{
  int i = lua_absindex(L, idx);
  lua_Integer value;
  int isnum;

  /* Action purpose: the LUA_TNUMBER gate is what refuses a string.
  lua_tointegerx() on its own accepts "5", "0x10" and even "  7  ", because it
  applies the string-to-number coercion the VM uses, and libucl accepted none of
  those. The gate passed, lua_tointegerx() then accepts an integral float --
  5.0, and 10/2, which is float division in Lua and visibly is five -- and
  refuses a fractional one rather than truncating it. */
  if (lua_type(L, i) != LUA_TNUMBER) {
    config_type_error(L, i, where, key, "integer");
    return false;
  }

  value = lua_tointegerx(L, i, &isnum);
  if (!isnum) {
    hikari_config_error(where, "\"%s\" expects a whole number, got %.14g", key,
        (double)lua_tonumber(L, i));
    return false;
  }

  *out = value;

  return true;
}

bool
hikari_config_double(lua_State *L,
    int idx,
    const char *where,
    const char *key,
    double *out)
{
  int i = lua_absindex(L, idx);

  if (lua_type(L, i) != LUA_TNUMBER) {
    config_type_error(L, i, where, key, "number");
    return false;
  }

  /* Both number subtypes are accepted, matching ucl_object_todouble_safe(), and
  behind the gate the conversion cannot fail. */
  *out = (double)lua_tonumber(L, i);

  return true;
}

/* Function purpose: Read a boolean, by exact type. This holds the only
lua_toboolean() call in the tree, and that is a reviewable rule: lua_toboolean()
cannot fail, so anywhere else it would silently turn a mistyped value -- 0, "",
an empty table -- into an enabled setting, which is precisely what libucl
rejected. */
bool
hikari_config_bool(lua_State *L,
    int idx,
    const char *where,
    const char *key,
    bool *out)
{
  int i = lua_absindex(L, idx);

  if (lua_type(L, i) != LUA_TBOOLEAN) {
    const char *word = NULL;

    if (lua_type(L, i) == LUA_TSTRING) {
      word = lua_tostring(L, i);
    }

    /* Action purpose: libucl spelled booleans six ways, case-insensitively.
    Lua has two. This lands on the user mechanically translating an old
    configuration, at the exact key that stopped working. */
    if (word != NULL &&
        (!strcasecmp(word, "yes") || !strcasecmp(word, "no") ||
            !strcasecmp(word, "on") || !strcasecmp(word, "off"))) {
      hikari_config_error(where,
          "\"%s\" expects a boolean -- write true or false, not \"%s\"", key,
          word);
    } else {
      config_type_error(L, i, where, key, "boolean");
    }

    return false;
  }

  *out = lua_toboolean(L, i) ? true : false;

  return true;
}

int
hikari_config_field(lua_State *L, int tidx, const char *key)
{
  int i = lua_absindex(L, tidx);

  /* Action purpose: lua_rawget on anything but a table is UNDEFINED BEHAVIOUR,
  not an error. Its api_check is compiled out of a release build of Lua, and
  measured against the library installed here both lua_rawget and lua_next
  SIGSEGV on a string -- a protected call does not catch it, because nothing is
  raised. The configuration controls these types, so `ui = "oops"` would be a
  crash rather than a diagnostic without this test, which also covers an index
  that is out of range: lua_type answers LUA_TNONE for one.

  LUA_TNONE is returned rather than LUA_TNIL so that the standard call shape,
  `if (hikari_config_field(...) != LUA_TNIL)`, takes its present-and-therefore-
  check-it branch and fails on the nil pushed below, instead of taking the
  absent branch and silently applying a default. */
  if (lua_type(L, i) != LUA_TTABLE) {
    hikari_config_error(NULL,
        "\"%s\" was looked up in %s, which is not a table", key,
        lua_typename(L, lua_type(L, i)));
    lua_pushnil(L);
    return LUA_TNONE;
  }

  /* tidx is absolutised before the push, because a relative index is relative
  to the top and the push moves it. The raw get is deliberate: the configuration
  has no metatables, a raw get cannot run an __index that raises, and raw is
  what "absent" should mean. */
  lua_pushstring(L, key);

  return lua_rawget(L, i);
}

void
hikari_config_iter_init(
    struct hikari_config_iter *iter, lua_State *L, int tidx)
{
  iter->tidx = lua_absindex(L, tidx);
  iter->top = lua_gettop(L);
  iter->vidx = 0;
  iter->failed = false;

  /* Action purpose: lua_next on anything but a table is undefined behaviour --
  see hikari_config_field. The walk is refused here, but the complaint is held
  back for the first next() call, because that is where the caller passes the
  name of the section and this function has no way to say which table was
  wrong. */
  if (lua_type(L, iter->tidx) != LUA_TTABLE) {
    iter->vidx = -1;
    return;
  }

  lua_pushnil(L);
}

bool
hikari_config_iter_next(struct hikari_config_iter *iter,
    lua_State *L,
    const char **key,
    const char *where)
{
  const char *name;
  size_t len;

  /* The report init could not word, now that `where` names the section. */
  if (iter->vidx == -1) {
    hikari_config_error(where, "expected a table, got %s",
        lua_typename(L, lua_type(L, iter->tidx)));
    iter->failed = true;
    iter->tidx = 0;
    iter->vidx = 0;

    return false;
  }

  /* The walk is over once tidx is cleared, so calling next() again after a
  false is a no-op rather than a lua_next() on whatever happens to be on top. */
  if (iter->tidx == 0) {
    return false;
  }

  /* Action purpose: lua_next() takes the cursor from the TOP of the stack, so
  the previous entry's value -- and anything the loop body left above it -- has
  to go before it is called, or the second call hands the value back as a key
  and raises "invalid key to 'next'". The cursor sits exactly one slot above
  the top recorded at init, so one settop() restores it however the body left
  the stack. */
  lua_settop(L, iter->top + 1);

  /* Action purpose: lua_next() pops the cursor key and pushes the next
  key/value pair, key at -2 and value at -1, or pushes nothing and returns 0
  when the table is exhausted. The key that stays on the stack IS the cursor,
  which is why nothing in this function converts it -- see the invariant at the
  top of the file. */
  if (lua_next(L, iter->tidx) == 0) {
    iter->tidx = 0;
    iter->vidx = 0;
    return false;
  }

  if (lua_type(L, -2) != LUA_TSTRING) {
    hikari_config_error(where,
        "key must be a string, got %s (write [\"0\"], not [0])",
        lua_typename(L, lua_type(L, -2)));
    goto failed;
  }

  name = lua_tolstring(L, -2, &len);
  if (strlen(name) != len) {
    hikari_config_error(where, "a key contains an embedded NUL byte");
    goto failed;
  }

  *key = name;
  iter->vidx = lua_gettop(L);

  return true;

failed:
  iter->failed = true;
  iter->tidx = 0;
  iter->vidx = 0;
  lua_pop(L, 2);

  return false;
}

void
hikari_config_iter_fini(struct hikari_config_iter *iter, lua_State *L)
{
  /* Unconditional, and the only correct way to leave a loop: settop() discards
  the cursor and the current value whatever path got here, so an iterator cannot
  be leaked by an early return the way the libucl ones were. */
  lua_settop(L, iter->top);

  iter->tidx = 0;
  iter->vidx = 0;
}

/* Function purpose: Lua's allocator, with a ceiling. Returning NULL for a
growing request makes the core raise LUA_ERRMEM, which the enclosing lua_pcall
turns into an ordinary diagnostic.

Action purpose: when ptr is NULL the osize argument is a type tag rather than a
size, so the old size must be taken as zero; this is the one part of the
lua_Alloc contract that is easy to get wrong and silently corrupts the running
total. */
static void *
config_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
  struct config_budget *budget = ud;
  size_t old = (ptr == NULL) ? 0 : osize;
  void *out;

  if (nsize == 0) {
    free(ptr);
    budget->used -= old;
    return NULL;
  }

  if (nsize > old && nsize - old > budget->cap - budget->used) {
    return NULL;
  }

  out = realloc(ptr, nsize);
  if (out == NULL) {
    return NULL;
  }

  budget->used = budget->used - old + nsize;

  return out;
}

/* Function purpose: Stop a configuration that will not terminate.

Action purpose: the hook stays ARMED across the raise, and that is the whole
point. It previously disarmed itself with lua_sethook(L, NULL, 0, 0) on the
reasoning that the raise must not re-enter it -- but lua_error() longjmps
immediately, so there is nothing to re-enter, and the disarm was permanent.
Nothing re-armed it, so the FIRST budget error removed the limit for the rest
of the load: a configuration that caught that error then ran unbounded and hung
the compositor at startup. Reproduced before this change, fixed after.

Staying armed is only half the fix. The other half is that pcall is no longer
in the sandbox whitelist (see config_whitelist below), so the configuration
cannot catch this error at all and it propagates to the lua_pcall that
hikari_config_lua_read() placed below everything. Either half alone is
insufficient: an armed hook whose error can be caught in a loop still never
terminates, because each catch simply re-raises.

The position is taken at level 0, not by luaL_error, which looks at level 1. A
hook fires inside the running function rather than below it, so level 1 is that
function's caller -- which for the main chunk does not exist, and the message
would then name no file and no line. Level 0 is the line the configuration was
spinning on. */
static void
config_budget_hook(lua_State *L, lua_Debug *ar)
{
  (void)ar;

  luaL_where(L, 0);
  lua_pushliteral(L, "the configuration exceeded its instruction budget");
  lua_concat(L, 2);
  lua_error(L);
}

static int
config_env_index(lua_State *L)
{
  const char *name;
  const char *value;
  size_t len;

  if (lua_type(L, 2) != LUA_TSTRING) {
    lua_pushnil(L);
    return 1;
  }

  name = lua_tolstring(L, 2, &len);
  value = (strlen(name) == len) ? getenv(name) : NULL;

  if (value == NULL) {
    lua_pushnil(L);
  } else {
    lua_pushstring(L, value);
  }

  return 1;
}

static int
config_env_newindex(lua_State *L)
{
  return luaL_error(L, "env is read-only");
}

/* Function purpose: Push the `env` table -- the successor to libucl's $NAME
expansion. An empty table that reaches getenv(3) through __index, so it is
word-bounded by construction, costs no copy of environ, and leaves an unset
variable as nil instead of the literal text of the reference.

Action purpose: __metatable = false makes getmetatable(env) answer false and
setmetatable(env, ...) raise, so a configuration cannot reach round the
read-only rule. Pushes one value. */
static void
config_push_env(lua_State *L)
{
  lua_newtable(L);

  lua_createtable(L, 0, 3);
  lua_pushcfunction(L, config_env_index);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, config_env_newindex);
  lua_setfield(L, -2, "__newindex");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "__metatable");

  lua_setmetatable(L, -2);
}

/* Pushes the directory part of `path`, which is CONFIG_DIR and the base an
include() resolves against. */
static void
config_push_dirname(lua_State *L, const char *path)
{
  const char *slash = strrchr(path, '/');

  if (slash == NULL) {
    lua_pushstring(L, ".");
  } else if (slash == path) {
    lua_pushstring(L, "/");
  } else {
    lua_pushlstring(L, path, (size_t)(slash - path));
  }
}

/* Function purpose: Turn a read of an undefined global into a located error.
libucl treated a bare word as a string, so `active = color3` meant "color3"; in
Lua it is a global read that would quietly be nil, and the key would simply be
absent. Nothing in a legitimate configuration reads an undefined global, so this
turns every bareword left unquoted by a hand translation, and every misspelled
helper, into a message with a file and a line. */
static int
config_global_index(lua_State *L)
{
  if (lua_type(L, 2) != LUA_TSTRING) {
    return luaL_error(L, "undefined variable");
  }

  return luaL_error(L, "undefined variable '%s'", lua_tostring(L, 2));
}

/* Function purpose: Resolve an include() argument against the directory of the
file that called it -- libucl's CURDIR semantics -- leaving an absolute path
alone.

Action purpose: the including file is read from the call stack rather than from
a variable this file maintains, so there is no bookkeeping to go stale if a
load is abandoned part way. ar.source is the full chunkname, "@" followed by
the path we passed to luaL_loadfilex; ar.short_src is the one Lua truncates.

The search walks UP the stack to the nearest file-sourced frame rather than
reading level 1 and stopping. Level 1 is not reliably a Lua frame: a C function
in the sandbox can call back into Lua -- table.sort's comparator is the reachable
example -- and such a frame reports ar.source as "=[C]". Taking that frame
leaves dir NULL, and the relative path is then resolved against the
compositor's current working directory instead of the configuration's, so the
include quietly reads the wrong file or none at all. */
static void
config_resolve_include(
    lua_State *L, const char *name, char *out, size_t outlen)
{
  lua_Debug ar;
  const char *dir = NULL;
  size_t dirlen = 0;
  int written;
  int level;

  for (level = 1; name[0] != '/' && lua_getstack(L, level, &ar); level++) {
    lua_getinfo(L, "S", &ar);

    if (ar.source == NULL || ar.source[0] != '@') {
      continue;
    }

    const char *slash = strrchr(ar.source + 1, '/');

    if (slash != NULL) {
      dir = ar.source + 1;
      dirlen = (size_t)(slash - dir);
    }

    break;
  }

  if (dir != NULL) {
    written = snprintf(out, outlen, "%.*s/%s", (int)dirlen, dir, name);
  } else {
    written = snprintf(out, outlen, "%s", name);
  }

  if (written < 0 || (size_t)written >= outlen) {
    luaL_error(L, "the included path \"%s\" is too long", name);
  }
}

/* Function purpose: Refuse an include that is too deep or that is already open.
Both answers come from the call stack, so there is no bookkeeping to go stale
if a load is abandoned part way.

Action purpose: only MAIN chunks are counted, which is what makes the depth a
count of open FILES. Testing `ar.source[0] == '@'` alone counts every
file-sourced frame, and an ordinary helper function defined in the
configuration is one of those -- so a file with a few helpers on the stack
consumed the include budget and a single legal include() failed with "includes
are nested more than 8 deep". ar.what is "main" for exactly one frame per file
being executed, which is the quantity this limit is about. The self-inclusion
test below needs the same restriction for the same reason: a helper defined in
file X and called from file Y would otherwise make including X look recursive. */
static void
config_check_include_stack(lua_State *L, const char *resolved)
{
  lua_Debug ar;
  int depth = 0;
  int level;

  for (level = 1; lua_getstack(L, level, &ar); level++) {
    lua_getinfo(L, "S", &ar);

    if (ar.source == NULL || ar.source[0] != '@') {
      continue;
    }

    if (ar.what == NULL || strcmp(ar.what, "main") != 0) {
      continue;
    }

    depth++;

    if (!strcmp(ar.source + 1, resolved)) {
      luaL_error(L, "\"%s\" includes itself", resolved);
    }
  }

  if (depth >= CONFIG_INCLUDE_DEPTH_MAX) {
    luaL_error(
        L, "includes are nested more than %d deep", CONFIG_INCLUDE_DEPTH_MAX);
  }
}

/* Function purpose: include(path [, optional]) -- the successor to libucl's
.include macro, and the only filesystem reach the sandbox keeps. That is parity,
not a new hole: a libucl configuration could already read another configuration
file. It returns the included chunk's single value for the author to splice,
because there is no sane in-place merge for a Lua table constructor. */
static int
config_include(lua_State *L)
{
  char resolved[4096];
  const char *name;
  const char *message;
  size_t len;
  bool optional = false;
  int status;
  const char *upvalue;

  if (lua_type(L, 1) != LUA_TSTRING) {
    return luaL_error(L, "include expects a file name");
  }

  name = lua_tolstring(L, 1, &len);
  if (strlen(name) != len) {
    return luaL_error(L, "the included path contains an embedded NUL byte");
  }

  if (!lua_isnoneornil(L, 2) &&
      !hikari_config_bool(L, 2, "include", "optional", &optional)) {
    return luaL_error(L, "include's second argument must be true or false");
  }

  config_resolve_include(L, name, resolved, sizeof(resolved));
  config_check_include_stack(L, resolved);

  status = luaL_loadfilex(L, resolved, "t");
  if (status != LUA_OK) {
    /* Action purpose: `optional` is libucl's .try_include and suppresses an
    unreadable file only. A file that is present but malformed is still fatal,
    because the author plainly meant to load it. */
    if (status == LUA_ERRFILE && optional) {
      lua_pop(L, 1);
      lua_pushnil(L);
      return 1;
    }

    message = (lua_type(L, -1) == LUA_TSTRING)
        ? lua_tostring(L, -1)
        : "cannot load the included file";

    return luaL_error(L, "%s", message);
  }

  lua_rawgetp(L, LUA_REGISTRYINDEX, &config_sandbox_key);

  upvalue = lua_setupvalue(L, -2, 1);
  if (upvalue == NULL || strcmp(upvalue, "_ENV")) {
    return luaL_error(L, "could not sandbox \"%s\"", resolved);
  }

  /* An unprotected call is safe here and only here: this function was itself
  reached through the lua_pcall that hikari_config_lua_read() put below
  everything. */
  lua_call(L, 0, 1);

  return 1;
}

/* Everything the configuration may name. A whitelist, never a blacklist: a
blacklist is wrong the moment a library gains a function.

load, loadfile and dofile are in the base library rather than in io or package,
so closing io and os does not close the filesystem -- these three are what
actually does. _G is excluded because it reaches the real globals whatever _ENV
says, collectgarbage because it lets a configuration stop the collector, and
warn because it has no use here. print stays: the configuration is a program
now, and a program you cannot print from is hostile to write.

Three further exclusions exist to keep the instruction budget enforceable, and
each was added after reproducing the failure it prevents:

pcall (and xpcall, never listed) would let a configuration CATCH the budget
error. Caught in a loop, the limit stops being a limit: the load never
terminates and the compositor never starts. The hook staying armed is not
enough on its own, because each catch merely re-raises. Without pcall the
budget error reaches hikari_config_lua_read()'s own lua_pcall and the load
fails cleanly, which is the required outcome. A configuration file has no use
for catching errors it should not be raising; assert and error remain.

setmetatable would let a configuration attach a __gc finalizer. Lua disables
debug hooks for the duration of a finalizer, so a non-terminating __gc is a
complete bypass of the budget, and at lua_close it runs outside any lua_pcall,
where a raise is abort(). getmetatable goes with it: the sandbox hands out the
same string, table and math metatables the host holds, and retrieving one is
only a step away from mutating shared state mid-load.

The shipped configuration uses none of the three. */
static const char *const config_whitelist[] = { "assert", "error", "ipairs",
  "next", "pairs", "print", "rawequal", "rawget", "rawlen", "rawset",
  "select", "tonumber", "tostring", "type",
  "_VERSION", "string", "table", "math", NULL };

static void
config_open_one(lua_State *L, const char *name, lua_CFunction opener)
{
  /* luaL_requiref is the only supported way to open one library: it runs the
  opener, caches the module in package.loaded and assigns the global. It leaves
  the module table on the stack. */
  luaL_requiref(L, name, opener, 1);
  lua_pop(L, 1);
}

/* Function purpose: Open the four libraries a configuration has a legitimate
use for and build the sandbox environment, which is left in the registry for the
chunk and for include(). Runs as a protected call because every step of it can
raise LUA_ERRMEM, and an unprotected raise is abort(). */
static int
config_prepare(lua_State *L)
{
  struct config_ctx *ctx = lua_touserdata(L, 1);
  size_t i;

  config_open_one(L, LUA_GNAME, luaopen_base);
  config_open_one(L, LUA_STRLIBNAME, luaopen_string);
  config_open_one(L, LUA_TABLIBNAME, luaopen_table);
  config_open_one(L, LUA_MATHLIBNAME, luaopen_math);

  lua_newtable(L);

  for (i = 0; config_whitelist[i] != NULL; i++) {
    lua_getglobal(L, config_whitelist[i]);
    lua_setfield(L, -2, config_whitelist[i]);
  }

  config_push_env(L);
  lua_setfield(L, -2, "env");

  lua_pushcfunction(L, config_include);
  lua_setfield(L, -2, "include");

  lua_pushstring(L, ctx->path);
  lua_setfield(L, -2, "CONFIG_FILE");

  config_push_dirname(L, ctx->path);
  lua_setfield(L, -2, "CONFIG_DIR");

  /* Action purpose: the metatable goes on last, so the fields above are plain
  assignments rather than reads through the guard. __metatable = false is what
  stops a configuration naming _ENV and taking the guard off again. */
  lua_createtable(L, 0, 2);
  lua_pushcfunction(L, config_global_index);
  lua_setfield(L, -2, "__index");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "__metatable");
  lua_setmetatable(L, -2);

  lua_rawsetp(L, LUA_REGISTRYINDEX, &config_sandbox_key);

  return 0;
}

/* Function purpose: Run the caller's reader inside a protected call, so that an
error anywhere in it -- including the LUA_ERRMEM a lua_pushstring can raise
while interning a key -- becomes a message instead of a panic. The configuration
table is passed as an argument because a lua_CFunction cannot see the stack
below its own frame, so an index into the caller's stack would not address
it. */
static int
config_reader_trampoline(lua_State *L)
{
  struct config_ctx *ctx = lua_touserdata(L, 1);

  ctx->verdict = ctx->reader(L, 2, ctx->ud);

  return 0;
}

bool
hikari_config_lua_read(const char *path, hikari_config_reader reader, void *ud)
{
  struct config_budget budget = { .used = 0, .cap = CONFIG_MEMORY_CAP };
  struct config_ctx ctx = {
    .path = path, .reader = reader, .ud = ud, .verdict = false
  };
  const char *outer_path = config_path;
  lua_State *L;
  const char *upvalue;
  bool success = false;
  int root;
  int status;

  config_path = path;

  L = lua_newstate(config_alloc, &budget);
  if (L == NULL) {
    hikari_config_error(NULL, "could not create the Lua interpreter");
    config_path = outer_path;
    return false;
  }

  lua_pushcfunction(L, config_prepare);
  lua_pushlightuserdata(L, &ctx);
  status = lua_pcall(L, 1, 0, 0);
  if (status != LUA_OK) {
    config_report(L, status);
    goto done;
  }

  /* Text mode only. The bytecode verifier is not a security boundary: malformed
  bytecode corrupts memory rather than erroring, so a configuration file is
  never loaded as anything but source. */
  status = luaL_loadfilex(L, path, "t");
  if (status != LUA_OK) {
    config_report(L, status);
    goto done;
  }

  /* Action purpose: upvalue 1 of a Lua main chunk is always _ENV, so replacing
  it redirects every free variable the chunk mentions into the sandbox at once.
  The returned name is checked because lua_setupvalue answers NULL when upvalue
  1 does not exist, and without the check a future change to chunk layout would
  leave the configuration attached to the real globals -- a sandbox that fails
  open. */
  lua_rawgetp(L, LUA_REGISTRYINDEX, &config_sandbox_key);
  upvalue = lua_setupvalue(L, -2, 1);
  if (upvalue == NULL || strcmp(upvalue, "_ENV")) {
    hikari_config_error(NULL, "could not install the configuration sandbox");
    goto done;
  }

  lua_sethook(L, config_budget_hook, LUA_MASKCOUNT, CONFIG_INSTRUCTION_BUDGET);
  status = lua_pcall(L, 0, 1, 0);
  lua_sethook(L, NULL, 0, 0);
  if (status != LUA_OK) {
    config_report(L, status);
    goto done;
  }

  if (lua_type(L, -1) != LUA_TTABLE) {
    hikari_config_error(NULL,
        "the configuration must return a table -- did you forget "
        "\"return { ... }\"?");
    goto done;
  }

  root = lua_gettop(L);

  lua_pushcfunction(L, config_reader_trampoline);
  lua_pushlightuserdata(L, &ctx);
  lua_pushvalue(L, root);
  status = lua_pcall(L, 2, 0, 0);
  if (status != LUA_OK) {
    config_report(L, status);
    goto done;
  }

  success = ctx.verdict;

done:
  /* lua_close releases everything the state owns however badly the read failed,
  which is what makes the borrowed-string rule safe to state so simply: the
  strings die here, all at once, and not before. */
  lua_close(L);
  config_path = outer_path;

  return success;
}
