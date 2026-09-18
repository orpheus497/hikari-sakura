// [COMMENT] Script function and purpose: Isolated setuid-root PAM authentication helper for unlocking screen sessions in hikari.

#include <pwd.h>
#include <security/pam_appl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

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
currently answering; hikari is the one that assigns meaning to it. */
struct hikari_unlock_request {
  uint64_t seq;
  uint32_t password_len;
};

struct hikari_unlock_reply {
  uint64_t seq;
  uint8_t success;
};

static char *input_buffer = NULL;

#define INPUT_BUFFER_SIZE 1024

// [COMMENT] Function purpose: Helper to robustly write the authentication
// reply, echoing back the request's own sequence number, to stdout fd 1.
static void write_reply(uint64_t seq, bool success) {
  struct hikari_unlock_reply reply = { .seq = seq, .success = success ? 1 : 0 };
  ssize_t nwritten;
  // [COMMENT] Action purpose: Retry write on EINTR. A single write() for
  // this small, fixed-size reply is atomic on a pipe (POSIX guarantees
  // atomicity for writes at least up to PIPE_BUF, far larger than this),
  // so no partial-write handling is needed here, matching the previous
  // single-write behavior for the bare bool this replaces.
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

      case PAM_ERROR_MSG:
      case PAM_TEXT_INFO:
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

  int pam_status = pam_authenticate(auth_handle, 0);

  // [COMMENT] Action purpose: Zero out sensitive password buffer immediately after authentication attempt.
  explicit_bzero(input_buffer, INPUT_BUFFER_SIZE);

  // [COMMENT] Action purpose: Distinguish fatal PAM errors from retryable
  // authentication failures. PAM_AUTH_ERR is a normal wrong-password result
  // that allows retry; all other non-success codes (PAM_ABORT, PAM_MAXTRIES,
  // PAM_SERVICE_ERR, PAM_SYSTEM_ERR) indicate unrecoverable failures.
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

  // [COMMENT] Action purpose: Write the authentication reply, echoing this
  // request's seq, to stdout fd 1.
  write_reply(request.seq, success);

  pam_end(auth_handle, pam_status);

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

  return 0;
}

