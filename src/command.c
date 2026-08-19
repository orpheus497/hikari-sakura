#include <hikari/command.h>

#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

void
hikari_command_execute(const char *cmd)
{
  pid_t child;
  int status;

  child = fork();
  if (child == 0) {
    child = fork();
    if (child == 0) {
      setsid();
      execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
      _exit(EXIT_FAILURE);
    }
    _exit(child == -1);
  }

  // [COMMENT] Action purpose: Reap the intermediate child, retrying on EINTR.
  while (waitpid(child, &status, 0) == -1 && errno == EINTR) {}
}
