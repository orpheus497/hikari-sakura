#include <hikari/switch_config.h>

/* [COMMENT] Action purpose: free(3). This was previously reached by accident
through <ucl.h>, which the configuration headers used to pull in; removing
libucl took the transitive include with it and left free() implicitly declared.
*/
#include <stdlib.h>

void
hikari_switch_config_fini(struct hikari_switch_config *switch_config)
{
  free(switch_config->switch_name);
}
