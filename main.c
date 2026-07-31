/* [COMMENT] Script function and purpose: Entrypoint for the hikari Wayland compositor, managing configuration paths, command line options, and server initialization. */

#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#ifndef NDEBUG
#include <wlr/util/log.h>
#endif

#include <hikari/server.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>

#include "version.h"

/* [COMMENT] Function purpose: Resolves the default configuration or autostart path relative to user environment variables. */
static char *
get_default_path(char *path)
{
  /* [COMMENT] Action purpose: Retrieve XDG_CONFIG_HOME or fallback to HOME directory. */
  char *prefix = getenv("XDG_CONFIG_HOME");
  char *subdirectory;

  /* [COMMENT] Action purpose: Check if XDG_CONFIG_HOME is set in environment. */
  if (prefix == NULL) {
    prefix = getenv("HOME");
    /* [COMMENT] Action purpose: Check if fallback HOME is set in environment. */
    if (prefix == NULL) {
      return NULL;
    }
    subdirectory = "/.config/hikari/";
  } else {
    subdirectory = "/hikari/";
  }

  /* [COMMENT] Action purpose: Allocate memory for concatenated path string. */
  size_t len = strlen(prefix) + strlen(subdirectory) + strlen(path);

  char *ret = malloc(len + 1);
  /* [COMMENT] Action purpose: Verify path buffer allocation succeeded. */
  if (ret == NULL) {
    return NULL;
  }

  /* [COMMENT] Action purpose: Construct prefix, subdirectory, and filename to return buffer. */
  /* [COMMENT] Action purpose: Verify snprintf path construction was not truncated. */
  if (snprintf(ret, len + 1, "%s%s%s", prefix, subdirectory, path) >= len + 1) {
    free(ret);
    return NULL;
  }

  return ret;
}

/* [COMMENT] Function purpose: Gets the user autostart script path. */
static char *
get_user_autostart(void)
{
  return get_default_path("autostart");
}

/* [COMMENT] Function purpose: Gets the user configuration file path. */
static char *
get_user_config_path(void)
{
  return get_default_path("hikari.conf");
}

#define STR(s) #s
#define DEFAULT_CONFIG(s) STR(s) "/etc/hikari/hikari.conf"
#define DEFAULT_CONFIG_FILE DEFAULT_CONFIG(HIKARI_ETC_PREFIX)

/* [COMMENT] Function purpose: Returns the default system-wide configuration path. */
static char *
get_default_config_path(void)
{
  return strdup(DEFAULT_CONFIG_FILE);
}

#undef STR
#undef DEFAULT_CONFIG
#undef DEFAULT_CONFIG_FILE

/* [COMMENT] Function purpose: Checks if a regular file exists and has specified access mode. */
static bool
check_perms(char *path, int mode)
{
  struct stat s;
  return stat(path, &s) == 0 && S_ISREG(s.st_mode) && !access(path, mode);
}

/* [COMMENT] Function purpose: Validates path accessibility and frees buffer if inaccessible. */
static char *
check_path(char *path, int mode)
{
  char *check = path;

  /* [COMMENT] Action purpose: Verify file permissions against requested mode. */
  if (!check_perms(check, mode)) {
    free(path);
    check = NULL;
  }

  return check;
}

/* [COMMENT] Function purpose: Resolves the active configuration path from CLI option, user config, or system default. */
static char *
get_config_path(char *path)
{
  char *config;

  /* [COMMENT] Action purpose: Select configuration source based on CLI parameter presence. */
  if (path != NULL) {
    char *option_config = check_path(path, R_OK);

    config = option_config;
  } else {
    char *user_config = check_path(get_user_config_path(), R_OK);

    /* [COMMENT] Action purpose: Fallback to system default configuration if user configuration is unavailable. */
    if (user_config == NULL) {
      char *default_config = check_path(get_default_config_path(), R_OK);

      config = default_config;
    } else {
      config = user_config;
    }
  }

  return config;
}

/* [COMMENT] Function purpose: Resolves the executable autostart path. */
static char *
get_autostart(char *path)
{
  char *autostart;
  int mode = R_OK | X_OK;

  /* [COMMENT] Action purpose: Check if CLI autostart option was provided. */
  if (path != NULL) {
    char *option_autostart = check_path(path, mode);

    autostart = option_autostart;
  } else {
    char *user_autostart = check_path(get_user_autostart(), mode);

    autostart = user_autostart;
  }

  return autostart;
}

const char *usage = "Usage: hikari [options]\n"
                    "\n"
                    "Options: \n"
                    "  -a <executable> Specify an autostart executable.\n"
                    "  -c <config>     Specify a configuration file.\n"
                    "  -h              Show this message and quit.\n"
                    "  -v              Show version and quit.\n"
                    "\n";

/* [COMMENT] Class purpose: Holds command line options parsed during invocation. */
struct options {
  char *config_path;
  char *autostart;
};

/* [COMMENT] Function purpose: Parses command line flags and resolves options structure. */
static void
parse_options(int argc, char **argv, struct options *options)
{
  char *config_path = NULL;
  char *autostart = NULL;

  char flag;
  /* [COMMENT] Action purpose: Process CLI flags using getopt. */
  while ((flag = getopt(argc, argv, "vhc:a:")) != -1) {
    /* [COMMENT] Action purpose: Switch on command line flag character. */
    switch (flag) {
      case 'a':
        free(autostart);
        autostart = strdup(optarg);
        break;

      case 'c':
        free(config_path);
        config_path = strdup(optarg);
        break;

      case 'v':
        free(config_path);
        free(autostart);

        printf("%s\n", HIKARI_VERSION);
        exit(EXIT_SUCCESS);
        break;

      case 'h':
        free(config_path);
        free(autostart);

        printf("%s", usage);
        exit(EXIT_SUCCESS);
        break;

      case '?':
      default:
        free(config_path);
        free(autostart);

        printf("%s", usage);
        exit(EXIT_FAILURE);
        break;
    }
  }

  options->config_path = get_config_path(config_path);
  options->autostart = get_autostart(autostart);
}

/* [COMMENT] Function purpose: Main entrypoint for launching hikari Wayland compositor. */
int
main(int argc, char **argv)
{
#ifndef NDEBUG
  wlr_log_init(WLR_DEBUG, NULL);
#endif
  struct options options;
  parse_options(argc, argv, &options);

  /* [COMMENT] Action purpose: Check if configuration file path was resolved successfully. */
  if (options.config_path == NULL) {
    free(options.autostart);

    /* [COMMENT] Action purpose: Report configuration load failure to stderr. */
    fprintf(stderr, "could not load configuration\n");

    return EXIT_FAILURE;
  } else {
    /* [COMMENT] Action purpose: Prepare backend security context and drop root privileges. */
    hikari_server_prepare_privileged();

    /* [COMMENT] Action purpose: Assert non-root execution context. */
    assert(geteuid() != 0 && geteuid() == getuid());
    assert(getegid() != 0 && getegid() == getgid());

    /* [COMMENT] Action purpose: Start hikari Wayland server event loop. */
    hikari_server_start(options.config_path, options.autostart);
    hikari_server_stop();

    return EXIT_SUCCESS;
  }
}

