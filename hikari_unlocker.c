/* ##Script function and purpose: Isolated setuid-root PAM authentication helper for unlocking screen sessions in hikari. */

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
    /* ##Error purpose: Return -1 if PAM initialization fails fatally. */
    return -1;
  }

  /* ##Action purpose: Read password string from stdin into locked buffer. */
  ssize_t nread;
  do {
    nread = read(0, input_buffer, INPUT_BUFFER_SIZE - 1);
  } while (nread == -1 && errno == EINTR);

  if (nread == -1) {
    pam_end(auth_handle, PAM_ABORT);
    return -1;
  }

  if (nread == INPUT_BUFFER_SIZE - 1 && input_buffer[INPUT_BUFFER_SIZE - 2] != '\n' && input_buffer[INPUT_BUFFER_SIZE - 2] != '\0') {
    char c;
    while (read(0, &c, 1) == 1 && c != '\n');
    explicit_bzero(input_buffer, INPUT_BUFFER_SIZE);
    pam_end(auth_handle, PAM_ABORT);
    return 0;
  }

  int pam_status = pam_authenticate(auth_handle, 0);

  /* ##Step purpose: Zero out sensitive password buffer immediately after authentication attempt. */
  explicit_bzero(input_buffer, INPUT_BUFFER_SIZE);
  success = (pam_status == PAM_SUCCESS);

  /* ##Action purpose: Write authentication success boolean result to stdout fd 1. */
  write(1, &success, sizeof(bool));

  pam_end(auth_handle, pam_status);

  return success ? 1 : 0;
}

/* ##Function purpose: Main entry point for PAM screen unlock helper executable. */
int
main(int argc, char **argv)
{
  char input;
  bool success = false;
  struct passwd *passwd = getpwuid(getuid());
  if (passwd == NULL) {
    return 1;
  }

  /* ##Step purpose: Allocate and lock password memory buffer in RAM to prevent swapping. */
  input_buffer = malloc(INPUT_BUFFER_SIZE);
  if (input_buffer == NULL) {
    return 1;
  }
  explicit_bzero(input_buffer, INPUT_BUFFER_SIZE);
  if (mlock(input_buffer, INPUT_BUFFER_SIZE) != 0) {
    free(input_buffer);
    return 1;
  }

  /* ##Loop purpose: Loop until valid authentication password is provided or fatal error. */
  while (!success) {
    int result = check_password(passwd->pw_name);
    if (result == -1) {
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

