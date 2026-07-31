/* ##Script function and purpose: Isolated setuid-root PAM authentication helper for unlocking screen sessions in hikari. */
#define _GNU_SOURCE
#define _DEFAULT_SOURCE

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

void explicit_bzero(void *s, size_t n);

static char *input_buffer = NULL;

#define INPUT_BUFFER_SIZE 1024

/* ##Function purpose: Helper to robustly write boolean result to stdout fd 1. */
static void write_success(bool success) {
  ssize_t nwritten;
  /* ##Loop purpose: Retry write on EINTR. */
  do {
    nwritten = write(1, &success, sizeof(bool));
  } while (nwritten == -1 && errno == EINTR);
}

/* ##Function purpose: Callback handler processing PAM authentication prompts. */
static int
conversation_handler(int num_msg,
    const struct pam_message **msg,
    struct pam_response **resp,
    void *data)
{
  /* ##Step purpose: Allocate memory for PAM response structure array. */
  struct pam_response *pam_reply = calloc(num_msg, sizeof(struct pam_response));

  /* ##Condition purpose: Check if allocation failed. */
  if (pam_reply == NULL) {
    /* ##Error purpose: Return abort status on memory allocation failure. */
    return PAM_ABORT;
  }
  *resp = pam_reply;

  /* ##Loop purpose: Process each PAM message in queue. */
  for (int i = 0; i < num_msg; ++i) {
    /* ##Condition purpose: Switch on PAM message prompt type. */
    switch (msg[i]->msg_style) {
      case PAM_PROMPT_ECHO_OFF:
      case PAM_PROMPT_ECHO_ON:
        pam_reply[i].resp = strdup(input_buffer);
        /* ##Condition purpose: Check if password string duplication succeeded. */
        if (pam_reply[i].resp == NULL) {
          /* ##Error purpose: Abort PAM handler on strdup failure. */
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

/* ##Function purpose: Authenticates input password against PAM subsystem for given username. */
int
check_password(const char *username)
{
  const struct pam_conv conv = {
    .conv = conversation_handler,
    .appdata_ptr = NULL,
  };

  bool success = false;
  pam_handle_t *auth_handle = NULL;

  /* ##Condition purpose: Initialize PAM authentication context. */
  if (pam_start("hikari-unlocker", username, &conv, &auth_handle) !=
      PAM_SUCCESS) {
    /* ##Error purpose: Return -1 and write false if PAM initialization fails fatally. */
    write_success(success);
    return -1;
  }

  /* ##Action purpose: Read password string from stdin into locked buffer until null terminator. */
  ssize_t nread = 0;
  ssize_t res;
  char c;
  bool overflow = false;
  /* ##Loop purpose: Retry password read on EINTR and accumulate until frame terminator is received. */
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

  /* ##Condition purpose: Check if read failed, returned EOF before terminator, or overflowed. */
  if (res <= 0 || overflow) {
    /* ##Error purpose: Abort PAM initialization and write false to stdout on read failure or overlong password. */
    if (overflow) {
      /* ##Loop purpose: Drain remaining stdin bytes until frame terminator to discard overlong input. */
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

  /* ##Action purpose: Zero out sensitive password buffer immediately after authentication attempt. */
  explicit_bzero(input_buffer, INPUT_BUFFER_SIZE);
  success = (pam_status == PAM_SUCCESS);

  /* ##Action purpose: Write authentication success boolean result to stdout fd 1. */
  write_success(success);

  pam_end(auth_handle, pam_status);

  return success ? 1 : 0;
}

/* ##Function purpose: Main entry point for PAM screen unlock helper executable. */
int
main(int argc, char **argv)
{
  bool success = false;
  struct passwd *passwd = getpwuid(getuid());
  /* ##Condition purpose: Verify password entry is found. */
  if (passwd == NULL) {
    /* ##Error purpose: Return 1 on missing password entry. */
    return 1;
  }

  /* ##Step purpose: Allocate and lock password memory buffer in RAM to prevent swapping. */
  input_buffer = malloc(INPUT_BUFFER_SIZE);
  /* ##Condition purpose: Verify input buffer allocation succeeded. */
  if (input_buffer == NULL) {
    /* ##Error purpose: Return 1 on allocation failure. */
    return 1;
  }
  explicit_bzero(input_buffer, INPUT_BUFFER_SIZE);
  /* ##Condition purpose: Verify memory lock succeeded to prevent swapping. */
  if (mlock(input_buffer, INPUT_BUFFER_SIZE) != 0) {
    /* ##Error purpose: Free buffer and return 1 on mlock failure. */
    free(input_buffer);
    return 1;
  }

  /* ##Loop purpose: Loop until valid authentication password is provided or fatal error. */
  while (!success) {
    int result = check_password(passwd->pw_name);
    /* ##Condition purpose: Check if authentication failed fatally. */
    if (result == -1) {
      /* ##Error purpose: Break authentication loop on fatal error. */
      break;
    } else if (result == 1) {
      success = true;
    }
  }

  /* ##Step purpose: Unlock and free secure password memory buffer prior to exit. */
  explicit_bzero(input_buffer, INPUT_BUFFER_SIZE);
  munlock(input_buffer, INPUT_BUFFER_SIZE);
  free(input_buffer);

  return 0;
}

