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
bool
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
    /* ##Error purpose: Return false if PAM initialization fails. */
    return false;
  }

  /* ##Action purpose: Read password string from stdin into locked buffer. */
  read(0, input_buffer, INPUT_BUFFER_SIZE - 1);
  int pam_status = pam_authenticate(auth_handle, 0);

  /* ##Step purpose: Zero out sensitive password buffer immediately after authentication attempt. */
  memset(input_buffer, 0, INPUT_BUFFER_SIZE);
  success = pam_status == PAM_SUCCESS;

  /* ##Action purpose: Write authentication success boolean result to stdout fd 1. */
  write(1, &success, sizeof(bool));

  pam_end(auth_handle, pam_status);

  return success;
}

/* ##Function purpose: Main entry point for PAM screen unlock helper executable. */
int
main(int argc, char **argv)
{
  char input;
  bool success = false;
  struct passwd *passwd = getpwuid(getuid());

  /* ##Step purpose: Allocate and lock password memory buffer in RAM to prevent swapping. */
  input_buffer = malloc(INPUT_BUFFER_SIZE);
  memset(input_buffer, 0, INPUT_BUFFER_SIZE);
  mlock(input_buffer, INPUT_BUFFER_SIZE);

  /* ##Loop purpose: Loop until valid authentication password is provided. */
  while (!success) {
    success = check_password(passwd->pw_name);
  }

  /* ##Step purpose: Unlock and free secure password memory buffer prior to exit. */
  munlock(input_buffer, INPUT_BUFFER_SIZE);
  free(input_buffer);

  return 0;
}

