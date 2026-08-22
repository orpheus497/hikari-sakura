// [COMMENT] Script function and purpose: Isolated setuid-root PAM authentication helper for unlocking screen sessions in hikari.

#include <pwd.h>
#include <security/pam_appl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>


static char *input_buffer = NULL;

#define INPUT_BUFFER_SIZE 1024

// [COMMENT] Function purpose: Helper to robustly write boolean result to stdout fd 1.
static void write_success(bool success) {
  ssize_t nwritten;
  // [COMMENT] Action purpose: Retry write on EINTR.
  do {
    nwritten = write(1, &success, sizeof(bool));
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

  // [COMMENT] Action purpose: Initialize PAM authentication context.
  if (pam_start("hikari-unlocker", username, &conv, &auth_handle) !=
      PAM_SUCCESS) {
    // [COMMENT] Action purpose: Return -1 and write false if PAM initialization fails fatally.
    write_success(success);
    return -1;
  }

  // [COMMENT] Action purpose: Read password string from stdin into locked buffer until null terminator.
  ssize_t nread = 0;
  ssize_t res;
  char c;
  bool overflow = false;
  // [COMMENT] Action purpose: Retry password read on EINTR and accumulate until frame terminator is received.
  do {
    res = read(0, &c, 1);
    if (res == -1 && errno == EINTR) {
      continue;
    }
    if (res <= 0) {
      break;
    }
    if (c == '\0') {
      break;
    }
    if (nread < INPUT_BUFFER_SIZE - 1) {
      input_buffer[nread++] = c;
    } else {
      overflow = true;
    }
  } while (1);

  // [COMMENT] Action purpose: Check if read failed, returned EOF before terminator, or overflowed.
  if (res <= 0 || overflow) {
    // [COMMENT] Action purpose: Abort PAM initialization and write false to stdout on read failure or overlong password.
    if (overflow) {
      // [COMMENT] Action purpose: Drain remaining stdin bytes until frame terminator to discard overlong input.
      while ((res = read(0, &c, 1)) == 1 || (res == -1 && errno == EINTR)) {
        if (res == 1 && c == '\0') break;
      }
    }
    explicit_bzero(input_buffer, INPUT_BUFFER_SIZE);
    write_success(success);
    pam_end(auth_handle, PAM_ABORT);
    return overflow ? 0 : -1;
  }

  input_buffer[nread] = '\0';


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
    write_success(success);
    pam_end(auth_handle, pam_status);
    return -1;
  }

  success = (pam_status == PAM_SUCCESS);

  // [COMMENT] Action purpose: Write authentication success boolean result to stdout fd 1.
  write_success(success);

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

