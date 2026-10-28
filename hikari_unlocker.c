// [COMMENT] Script function and purpose: Isolated setuid-root PAM authentication helper for unlocking screen sessions in hikari.

#include <pwd.h>
#include <security/pam_appl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

/* [COMMENT] Action purpose: explicit_bzero() for a C library that does not
have one. This file scrubs password buffers in six places and, before this
include, could only ever get that symbol from libc -- its rule passed no -I at
all, so the shim written for exactly this case was unreachable from the one
binary where a failed scrub matters most. On FreeBSD the header expands to
<strings.h> and <unistd.h> and nothing else, so the preprocessed output of a
FreeBSD build is unchanged and this costs that platform nothing.

This does NOT undo the independence the wire-format comment below describes.
That independence is about the two IPC structs, which are duplicated on purpose
so each end pins its own layout with _Static_assert; compat.h declares no type,
allocates nothing, and links no library. */
#include <hikari/compat.h>

/* Wire format for the password-submission pipe from hikari (lock_mode.c).
Must be kept byte-for-byte identical to the copy of these two structs
there -- there is deliberately no shared header, so this binary's build
stays independent of the rest of the tree. Both ends are the same
architecture and compiler (this process is forked directly from hikari),
so native struct layout is fine; this is local IPC framing, not a network
protocol needing byte-order/alignment portability.

Replaces a previous bare byte-in/byte-out protocol (a NUL-terminated
password in, a single result byte out) that gave the reader no way to
tell a stale, already-superseded result apart from the one answering the
password just submitted. The `seq` field is opaque here -- this process
only ever echoes back whatever seq arrived with the request it is
currently answering; hikari is the one that assigns meaning to it.

The reply also carries a bounded diagnostic line, so that a refusal PAM
can explain does not reach the user as a bare "denied". It is a
FIXED-SIZE field rather than a length-framed trailer, and the whole
reply is sized to exactly 512 bytes, because a write(2) of PIPE_BUF
bytes or fewer is atomic on a pipe and PIPE_BUF is 512 on FreeBSD (4096
on Linux). At 512 the reply stays one atomic write here and one
exact-size read on the compositor side, inside a Wayland event-loop
callback on a blocking descriptor. A length-framed reply would force
that callback to accumulate across partial reads, which is a way to hang
the whole compositor. Truncating a diagnostic is acceptable;
desynchronising this pipe is not. */
#define HIKARI_UNLOCK_MESSAGE_MAX 500

struct hikari_unlock_request {
  uint64_t seq;
  uint32_t password_len;
};

struct hikari_unlock_reply {
  uint64_t seq;
  uint8_t success;
  uint8_t message_is_error;
  uint16_t message_len;
  char message[HIKARI_UNLOCK_MESSAGE_MAX];
};

/* [COMMENT] Action purpose: Pin the wire layout as literals. The two copies of
these structs live in separate files on purpose -- this binary must not depend
on the tree's headers -- and until now nothing but prose kept them in step. A
silent divergence is invisible at runtime rather than loud: the compositor
would read a wrong `seq`, discard every reply as stale, and the lock screen
would refuse every password with no diagnostic whatsoever. A _Static_assert
cannot compare two translation units, so each file pins the same numbers
independently; change them here and in the matching block in src/lock_mode.c
together, or whichever file was not updated fails to build, which is the point.

The 512 on the reply is also the FreeBSD PIPE_BUF atomicity bound described
above, so that number is load-bearing twice over. */
_Static_assert(sizeof(struct hikari_unlock_request) == 16, "wire format");
_Static_assert(offsetof(struct hikari_unlock_request, seq) == 0, "wire format");
_Static_assert(
    offsetof(struct hikari_unlock_request, password_len) == 8, "wire format");

_Static_assert(sizeof(struct hikari_unlock_reply) == 512, "wire format");
_Static_assert(offsetof(struct hikari_unlock_reply, seq) == 0, "wire format");
_Static_assert(
    offsetof(struct hikari_unlock_reply, success) == 8, "wire format");
_Static_assert(
    offsetof(struct hikari_unlock_reply, message_is_error) == 9, "wire format");
_Static_assert(
    offsetof(struct hikari_unlock_reply, message_len) == 10, "wire format");
_Static_assert(
    offsetof(struct hikari_unlock_reply, message) == 12, "wire format");

static char *input_buffer = NULL;

#define INPUT_BUFFER_SIZE 1024

/* [COMMENT] Action purpose: PAM's running commentary on the attempt currently
in flight. Modules explain themselves through PAM_TEXT_INFO and PAM_ERROR_MSG
during the conversation, well before the verdict is known -- pam_faillock's
"The account is locked due to 3 failed logins" and pam_unix's "Your account has
expired" both arrive that way -- so the text has to be accumulated as it
appears and attached to whichever reply is eventually written. Reset at the top
of every check_password(), so attempt N's explanation can never be shown
against attempt N+1. */
static char message_buffer[HIKARI_UNLOCK_MESSAGE_MAX];
static size_t message_len;
static bool message_is_error;

/* [COMMENT] Function purpose: Decode one UTF-8 sequence at `s`, rejecting
everything a strict decoder must reject: a bad lead byte, a sequence truncated
by `len`, a bad continuation byte, an overlong encoding, a UTF-16 surrogate and
anything above U+10FFFF. Returns the bytes consumed, or 0 when the sequence is
not well formed -- so one call both validates and advances, and a caller can
walk a string without ever splitting a character.

Written out here rather than shared with the compositor's copy because this
binary deliberately includes no header from the tree. */
static size_t
utf8_decode(const char *s, size_t len, uint32_t *codepoint)
{
  static const uint32_t minimum[5] = { 0, 0, 0x80, 0x800, 0x10000 };

  const unsigned char *bytes = (const unsigned char *)s;
  size_t length;
  uint32_t value;

  if (len == 0) {
    return 0;
  }

  if (bytes[0] < 0x80) {
    length = 1;
    value = bytes[0];
  } else if ((bytes[0] & 0xe0) == 0xc0) {
    length = 2;
    value = bytes[0] & 0x1f;
  } else if ((bytes[0] & 0xf0) == 0xe0) {
    length = 3;
    value = bytes[0] & 0x0f;
  } else if ((bytes[0] & 0xf8) == 0xf0) {
    length = 4;
    value = bytes[0] & 0x07;
  } else {
    return 0;
  }

  if (length > len) {
    return 0;
  }

  for (size_t i = 1; i < length; ++i) {
    if ((bytes[i] & 0xc0) != 0x80) {
      return 0;
    }
    value = (value << 6) | (bytes[i] & 0x3f);
  }

  if (value < minimum[length] || value > 0x10ffff ||
      (value >= 0xd800 && value <= 0xdfff)) {
    return 0;
  }

  *codepoint = value;

  return length;
}

/* [COMMENT] Function purpose: Append one PAM message to the accumulator that
will ride back with the next reply, sanitised and bounded.

[COMMENT] Action purpose: The text originates in PAM modules running as root
and is about to be rendered by the compositor on the one surface that must
never misbehave, so it is treated as untrusted on the way OUT of this process
rather than only on the way in. Three rules. Reject the whole message if it is
not valid UTF-8, because Pango's behaviour on malformed input is not something
a lock screen should discover at runtime. Drop EVERY control codepoint -- C0,
DEL and C1, the newline included -- so nothing can smuggle a NUL into a
fixed-size field, an escape sequence onto a terminal, or an unbounded number of
lines into the compositor's renderer. Truncate on a character boundary when the
accumulator is full, because a half-written sequence is precisely the malformed
UTF-8 the first rule exists to keep out.

The newline is excluded on measured grounds rather than taste. The compositor
caps the rendered message at three lines with pango_layout_set_height(-3), and
that cap holds for wrapped text but NOT across explicit line breaks: measured
with pangocairo on this host, 500 bytes of text wrap to exactly 3 lines and
600x51 pixels however they are spaced, while 250 repetitions of "x\n" produce
251 lines and a 4267-pixel-tall layout. Several messages are therefore joined
with a visible separator instead, which keeps the whole diagnostic one
wrappable paragraph and the buffer bounded whatever a module emits.

Truncation is silent by design: Linux-PAM caps a single message at
PAM_MAX_MSG_SIZE (512), larger than this whole field, so a long message is
expected rather than exceptional. */
static void
append_message(const char *text, bool is_error)
{
  if (text == NULL) {
    return;
  }

  size_t len = strlen(text);
  size_t offset = 0;

  while (offset < len) {
    uint32_t codepoint;
    size_t consumed = utf8_decode(text + offset, len - offset, &codepoint);

    if (consumed == 0) {
      return;
    }

    offset += consumed;
  }

  // [COMMENT] Action purpose: Remember where this message started, so a message
  // that sanitises away to nothing does not leave the separator behind as a
  // blank line.
  size_t restore_to = message_len;

  // [COMMENT] Action purpose: A visible separator rather than a newline, for
  // the reason measured above. Skipped rather than truncated when it will not
  // fit, so a full accumulator never ends in a dangling separator.
  static const char separator[] = " | ";

  if (message_len != 0) {
    if (message_len + sizeof(separator) - 1 > sizeof(message_buffer)) {
      return;
    }
    memcpy(message_buffer + message_len, separator, sizeof(separator) - 1);
    message_len += sizeof(separator) - 1;
  }

  size_t body_start = message_len;

  offset = 0;
  while (offset < len) {
    uint32_t codepoint;
    size_t consumed = utf8_decode(text + offset, len - offset, &codepoint);

    // [COMMENT] Action purpose: Unreachable -- the validating pass above walked
    // this exact string and rejected it if any sequence was malformed. Kept
    // because the alternative to this branch is an infinite loop in a
    // setuid-root process, which is not a failure mode worth leaving to an
    // invariant holding two loops apart.
    if (consumed == 0) {
      break;
    }

    offset += consumed;

    if (codepoint < 0x20 || codepoint == 0x7f ||
        (codepoint >= 0x80 && codepoint <= 0x9f)) {
      continue;
    }

    if (message_len + consumed > sizeof(message_buffer)) {
      break;
    }

    memcpy(message_buffer + message_len, text + offset - consumed, consumed);
    message_len += consumed;
  }

  if (message_len == body_start) {
    message_len = restore_to;
    return;
  }

  if (is_error) {
    message_is_error = true;
  }
}

// [COMMENT] Function purpose: Helper to robustly write the authentication
// reply, echoing back the request's own sequence number, to stdout fd 1.
static void write_reply(uint64_t seq, bool success) {
  struct hikari_unlock_reply reply;

  // [COMMENT] Action purpose: Zero the whole struct before filling it, rather
  // than designated-initialising the named members. The message field is
  // fixed-size and almost never full, and its unused tail plus any padding
  // would otherwise be whatever happened to be on this process's stack --
  // written verbatim down a pipe by a setuid-root binary.
  memset(&reply, 0, sizeof(reply));

  reply.seq = seq;
  reply.success = success ? 1 : 0;
  reply.message_is_error = message_is_error ? 1 : 0;
  reply.message_len = (uint16_t)message_len;
  memcpy(reply.message, message_buffer, message_len);

  ssize_t nwritten;
  // [COMMENT] Action purpose: Retry write on EINTR. A single write() for this
  // fixed-size reply is atomic on a pipe: POSIX guarantees atomicity up to
  // PIPE_BUF, which is 512 on FreeBSD and 4096 on Linux, and the struct is
  // sized to exactly 512 so that the guarantee holds on BOTH. No partial-write
  // handling is needed here and none must become needed -- the reader is a
  // blocking read inside the compositor's event loop. The _Static_assert on
  // sizeof above is what stops the struct outgrowing the guarantee unnoticed.
  do {
    nwritten = write(1, &reply, sizeof(reply));
  } while (nwritten == -1 && errno == EINTR);
}

// [COMMENT] Function purpose: Callback handler processing PAM authentication prompts.
static int
conversation_handler(int num_msg,
    const struct pam_message **msg,
    struct pam_response **resp,
    void *data)
{
  // [COMMENT] Action purpose: Allocate memory for PAM response structure array.
  struct pam_response *pam_reply = calloc(num_msg, sizeof(struct pam_response));

  // [COMMENT] Action purpose: Check if allocation failed.
  if (pam_reply == NULL) {
    // [COMMENT] Action purpose: Return abort status on memory allocation failure.
    return PAM_ABORT;
  }
  *resp = pam_reply;

  // [COMMENT] Action purpose: Process each PAM message in queue.
  for (int i = 0; i < num_msg; ++i) {
    // [COMMENT] Action purpose: Switch on PAM message prompt type.
    switch (msg[i]->msg_style) {
      case PAM_PROMPT_ECHO_OFF:
      case PAM_PROMPT_ECHO_ON:
        pam_reply[i].resp = strdup(input_buffer);
        // [COMMENT] Action purpose: Check if password string duplication succeeded.
        if (pam_reply[i].resp == NULL) {
          // [COMMENT] Action purpose: Abort PAM handler on strdup failure.
          for (int j = 0; j < i; ++j) {
            free(pam_reply[j].resp);
          }
          free(pam_reply);
          *resp = NULL;
          return PAM_ABORT;
        }
        break;

      // [COMMENT] Action purpose: PAM explaining itself. Previously dropped on
      // the floor, which is what makes a pam_faillock lockout unexplainable:
      // the tally returns PAM_AUTH_ERR for the CORRECT password until the
      // timeout expires, and the only place PAM says so is a PAM_TEXT_INFO.
      // Accumulated here and relayed with the reply instead.
      case PAM_ERROR_MSG:
        append_message(msg[i]->msg, true);
        break;

      case PAM_TEXT_INFO:
        append_message(msg[i]->msg, false);
        break;
    }
  }
  return PAM_SUCCESS;
}

// [COMMENT] Function purpose: Authenticates input password against PAM subsystem for given username.
int
check_password(const char *username)
{
  const struct pam_conv conv = {
    .conv = conversation_handler,
    .appdata_ptr = NULL,
  };

  bool success = false;
  pam_handle_t *auth_handle = NULL;

  // [COMMENT] Action purpose: Start every attempt with an empty accumulator.
  // The helper loops in main() across attempts, so without this reset the
  // explanation for a previous wrong password would still be attached to the
  // reply for the next one.
  explicit_bzero(message_buffer, sizeof(message_buffer));
  message_len = 0;
  message_is_error = false;

  // [COMMENT] Action purpose: Read the fixed-size request header first,
  // accumulating across partial reads (a genuine possibility for a
  // larger, non-atomic transfer, unlike the small fixed-size reply this
  // process writes back) and retrying on EINTR.
  struct hikari_unlock_request request;
  size_t header_read = 0;
  while (header_read < sizeof(request)) {
    ssize_t res = read(
        0, (unsigned char *)&request + header_read, sizeof(request) - header_read);
    if (res == -1 && errno == EINTR) {
      continue;
    }
    if (res <= 0) {
      // [COMMENT] Action purpose: EOF or error before a complete header
      // was ever read -- the parent closed its end (e.g. during
      // shutdown) or something is badly wrong. There is no reliably-read
      // seq to reply with here, so exit without one, exactly as the
      // parent's own hangup handling already expects for "no usable
      // result at all".
      return -1;
    }
    header_read += (size_t)res;
  }

  // [COMMENT] Action purpose: Reject a request whose claimed password
  // length exceeds this process's own buffer before reading a single
  // byte of it. The parent should never construct a request larger than
  // its own matching buffer size; a mismatch here means something is
  // badly wrong (protocol drift between binaries, memory corruption), and
  // failing fast is safer than any attempt to interpret or resynchronize
  // an explicitly length-framed stream.
  if (request.password_len > INPUT_BUFFER_SIZE - 1) {
    write_reply(request.seq, false);
    return -1;
  }

  // [COMMENT] Action purpose: Initialize PAM authentication context.
  if (pam_start("hikari-unlocker", username, &conv, &auth_handle) !=
      PAM_SUCCESS) {
    // [COMMENT] Action purpose: Reply false and return -1 if PAM
    // initialization fails fatally.
    write_reply(request.seq, success);
    return -1;
  }

  // [COMMENT] Action purpose: Read exactly password_len bytes into the
  // locked buffer, accumulating across partial reads and retrying on
  // EINTR -- the length is now explicit, so there is no terminator to
  // scan for and no possibility of an overlong read.
  size_t body_read = 0;
  while (body_read < request.password_len) {
    ssize_t res = read(
        0, input_buffer + body_read, request.password_len - body_read);
    if (res == -1 && errno == EINTR) {
      continue;
    }
    if (res <= 0) {
      explicit_bzero(input_buffer, INPUT_BUFFER_SIZE);
      write_reply(request.seq, false);
      pam_end(auth_handle, PAM_ABORT);
      return -1;
    }
    body_read += (size_t)res;
  }

  input_buffer[request.password_len] = '\0';

  /* [COMMENT] Action purpose: PAM_DISALLOW_NULL_AUTHTOK refuses to authenticate
  an account whose stored password is empty. Without it the flag argument is 0
  and the decision is inherited from the system stack, which on every Linux
  family checked says the wrong thing for a screen locker: Debian/Ubuntu's
  common-auth, Fedora's authselect-rendered system-auth and Arch's pambase
  system-auth all carry `pam_unix.so ... nullok`. nullok is defensible for a
  console login on a machine with no password set; it is not defensible for
  unlocking a session that is already running, where it means anyone walking
  past can dismiss the lock screen by pressing Enter. FreeBSD is affected by
  this change too, and deliberately so -- the reasoning is not platform
  specific. */
  int pam_status = pam_authenticate(auth_handle, PAM_DISALLOW_NULL_AUTHTOK);

  // [COMMENT] Action purpose: Zero out sensitive password buffer immediately after authentication attempt.
  explicit_bzero(input_buffer, INPUT_BUFFER_SIZE);

  /* [COMMENT] Action purpose: Distinguish fatal PAM errors from retryable
  authentication failures. PAM_AUTH_ERR is a normal wrong-password result that
  allows retry; every other non-success code is treated as unrecoverable and
  exits.

  Two corrections to the earlier reading of this branch, both established by
  running a probe against real stacks rather than from the documentation:

  PAM_MAXTRIES does not arrive here. It was previously listed as the lockout
  code, but pam_faillock(8) documents PAM_AUTH_ERR for "too many failed logins"
  and that is what a locked tally actually returns -- so a lockout presents as
  an ordinary wrong password and stays on the retryable path, which is correct:
  the user may still type the right password once the tally expires.

  What a lockout must not do is present as an ordinary wrong password to the
  PERSON. On a stack with pam_faillock in the auth group (Arch ships one
  unconditionally; Fedora gates it behind with-faillock), three wrong attempts
  lock the account, after which the CORRECT password also returns
  PAM_AUTH_ERR until the timeout expires. PAM explains that through a
  PAM_TEXT_INFO, and the explanation is now relayed: conversation_handler
  above captures both PAM_TEXT_INFO and PAM_ERROR_MSG through
  append_message(), struct hikari_unlock_reply carries message_is_error,
  message_len and message, and src/lock_mode.c sanitises the text and hands it
  to hikari_lock_indicator_set_message() for display beneath the indicator. So
  the refusal says why it is refusing rather than looking like a typo.

  PAM_PERM_DENIED (6) is reachable and was absent from this list. It is what a
  structurally broken policy produces -- an `include` naming a stack that does
  not exist returns it from pam_authenticate rather than failing at pam_start.
  It is deliberately left on the fatal path: it signals a configuration fault,
  not a bad password, and retrying would loop forever against a stack that can
  never succeed. */
  if (pam_status != PAM_SUCCESS && pam_status != PAM_AUTH_ERR) {
    // [COMMENT] Action purpose: Deny explicitly before exiting, matching the
    // pam_start failure path above. This was the only terminal path in the
    // helper that wrote no result byte at all, leaving the compositor to infer
    // the failure from the pipe hangup that follows process exit -- so the
    // deny indicator waited on process teardown instead of appearing at once.
    // success is still false here, so the attempt fails closed either way;
    // what changes is that the compositor is told, rather than left to deduce
    // it. locker_result_handler already handles the READABLE|HANGUP pair this
    // produces, since the child exits immediately afterwards.
    write_reply(request.seq, success);
    pam_end(auth_handle, pam_status);
    return -1;
  }

  success = (pam_status == PAM_SUCCESS);

  // [COMMENT] Action purpose: The status handed to pam_end(). It becomes the
  // account-management verdict whenever that is what refused, so the modules'
  // cleanup sees why the session was abandoned rather than the now-stale
  // PAM_SUCCESS from authentication.
  int end_status = pam_status;

  /* [COMMENT] Action purpose: Account management -- the call that makes
  `account` lines in the installed policy mean anything at all. Without it they
  were inert: an expired account, an expired password, a pam_time window and a
  faillock tally evaluated in the `account` group were all ignored on unlock.

  Order and guard are both load-bearing. It runs AFTER pam_authenticate and
  ONLY on PAM_SUCCESS. On an unauthenticated handle it would leak account state
  -- whether an account exists, whether it has expired -- to whoever is
  standing at the lock screen; after a wrong password it would evaluate a
  faillock tally a second time for every typo. input_buffer has already been
  cleared above and is deliberately not held across this call: account
  management has no business with a password.

  Every code is classified deliberately. The codes and their accompanying
  conversation messages were MEASURED against Linux-PAM 1.5.3 rather than
  taken from documentation:

    * PAM_SUCCESS -- the account may be used right now. The only code that
      unlocks unqualified.

    * PAM_NEW_AUTHTOK_REQD (measured: 12, from pam_unix after `chage -d 0`,
      alongside PAM_ERROR_MSG "You are required to change your password
      immediately") -- UNLOCKS, with the explanation relayed. This is the one
      place where refusing would be worse than allowing. A screen locker cannot
      run pam_chauthtok(): there is no old/new/confirm dialogue, and
      conversation_handler above answers every prompt from the same single
      buffered string, so a change-password conversation would feed the old
      password to all three prompts. Treating it as failure would mean a user
      whose password expires WHILE THEIR SESSION IS RUNNING can never unlock
      it, leaving the session this locker exists to protect reachable only by
      killing it. Nor is it a widening of what counts as a successful unlock:
      the password has already been proved, this is exactly what happened
      before this call existed, and it is no longer silent, because the reply
      now carries the reason. The lock screen is gone by the time a successful
      unlock is drawn, so the compositor logs this one rather than drawing it.

    * PAM_AUTH_ERR (measured: 7, both from `account required pam_deny.so` and
      from Debian's common-account when its pam_unix arm fails) -- RETRYABLE,
      the same classification this code already has for pam_authenticate. It is
      the one account-group refusal that is plausibly transient, a faillock
      tally being the obvious case, so the helper stays alive for the next
      attempt instead of exiting.

    * Everything else -- PAM_ACCT_EXPIRED (measured: 13), PAM_USER_UNKNOWN
      (measured: 10), PAM_PERM_DENIED, and any code not listed -- FATAL.
      These are configuration or account facts rather than a mistyped
      password; retrying cannot change them and looping would spin forever
      against a stack that can never succeed. This is where PAM_PERM_DENIED
      from pam_authenticate already lands.

  No refusal here is silent. Where the account stack explained itself, the
  conversation handler has already captured the text; where it did not --
  PAM_USER_UNKNOWN produced no message at all when measured -- pam_strerror()
  supplies one, so the lock screen never shows a bare refusal the user has no
  way to act on. */
  if (success) {
    size_t message_len_before = message_len;

    int acct_status = pam_acct_mgmt(auth_handle, PAM_DISALLOW_NULL_AUTHTOK);

    if (acct_status != PAM_SUCCESS) {
      end_status = acct_status;

      if (message_len == message_len_before) {
        append_message(pam_strerror(auth_handle, acct_status), true);
      }

      switch (acct_status) {
        case PAM_NEW_AUTHTOK_REQD:
          break;

        case PAM_AUTH_ERR:
          success = false;
          break;

        default:
          write_reply(request.seq, false);
          pam_end(auth_handle, end_status);
          return -1;
      }
    }
  }

  // [COMMENT] Action purpose: Write the authentication reply, echoing this
  // request's seq, to stdout fd 1.
  write_reply(request.seq, success);

  pam_end(auth_handle, end_status);

  return success ? 1 : 0;
}

// [COMMENT] Function purpose: Main entry point for PAM screen unlock helper executable.
int
main(int argc, char **argv)
{
  bool success = false;
  struct passwd *passwd = getpwuid(getuid());
  // [COMMENT] Action purpose: Verify password entry is found.
  if (passwd == NULL) {
    // [COMMENT] Action purpose: Return 1 on missing password entry.
    return 1;
  }

  // [COMMENT] Action purpose: Allocate and lock password memory buffer in RAM to prevent swapping.
  input_buffer = malloc(INPUT_BUFFER_SIZE);
  // [COMMENT] Action purpose: Verify input buffer allocation succeeded.
  if (input_buffer == NULL) {
    // [COMMENT] Action purpose: Return 1 on allocation failure.
    return 1;
  }
  explicit_bzero(input_buffer, INPUT_BUFFER_SIZE);
  // [COMMENT] Action purpose: Verify memory lock succeeded to prevent swapping.
  if (mlock(input_buffer, INPUT_BUFFER_SIZE) != 0) {
    // [COMMENT] Action purpose: Free buffer and return 1 on mlock failure.
    free(input_buffer);
    return 1;
  }

  // [COMMENT] Action purpose: Loop until valid authentication password is provided or fatal error.
  while (!success) {
    int result = check_password(passwd->pw_name);
    // [COMMENT] Action purpose: Check if authentication failed fatally.
    if (result == -1) {
      // [COMMENT] Action purpose: Break authentication loop on fatal error.
      break;
    } else if (result == 1) {
      success = true;
    }
  }

  // [COMMENT] Action purpose: Unlock and free secure password memory buffer prior to exit.
  explicit_bzero(input_buffer, INPUT_BUFFER_SIZE);
  munlock(input_buffer, INPUT_BUFFER_SIZE);
  free(input_buffer);

  // [COMMENT] Action purpose: PAM's diagnostics are not secret, but the buffer
  // holding them sits beside one that is and is cleared on the same paths, so
  // that neither is ever the one that was forgotten.
  explicit_bzero(message_buffer, sizeof(message_buffer));

  return 0;
}

