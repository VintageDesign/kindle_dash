#include "config.h"
#include "page.h"

char *cfg_string(GKeyFile *cfg, const char *section, const char *key, const char *def)
{
    char *v = g_key_file_get_string(cfg, section, key, NULL);
    if (v) {
        g_strstrip(v);
        return v;
    }
    return def ? g_strdup(def) : NULL;
}

int cfg_int(GKeyFile *cfg, const char *section, const char *key, int def)
{
    GError *err = NULL;
    int v = g_key_file_get_integer(cfg, section, key, &err);
    if (err) {
        g_error_free(err);
        return def;
    }
    return v;
}

gboolean cfg_bool(GKeyFile *cfg, const char *section, const char *key, gboolean def)
{
    GError *err = NULL;
    gboolean v = g_key_file_get_boolean(cfg, section, key, &err);
    if (err) {
        g_error_free(err);
        return def;
    }
    return v;
}

char *cfg_path(GKeyFile *cfg, const char *section, const char *key, const char *def)
{
    char *v = cfg_string(cfg, section, key, def);
    if (v && v[0] == '~' && (v[1] == '/' || v[1] == '\0')) {
        char *home = g_build_filename(g_get_home_dir(), v + 1, NULL);
        g_free(v);
        return home;
    }
    if (v && *v && !g_path_is_absolute(v)) {
        char *abs = g_build_filename(app_config_dir(), v, NULL);
        g_free(v);
        return abs;
    }
    return v;
}
