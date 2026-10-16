#ifndef KD_CONFIG_H
#define KD_CONFIG_H

#include <glib.h>

/* Small wrappers over GKeyFile that fall back to a default when a key is missing. */
char *cfg_string(GKeyFile *cfg, const char *section, const char *key, const char *def);
int cfg_int(GKeyFile *cfg, const char *section, const char *key, int def);
gboolean cfg_bool(GKeyFile *cfg, const char *section, const char *key, gboolean def);
/* Like cfg_string, for paths: absolute paths are used as-is, "~/..." expands to the
 * home directory, and other relative paths resolve against the config directory. */
char *cfg_path(GKeyFile *cfg, const char *section, const char *key, const char *def);

#endif
