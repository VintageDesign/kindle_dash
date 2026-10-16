#include "pages.h"
#include "config.h"

#include <string.h>

extern const PageType clock_type;
extern const PageType weather_type;
extern const PageType svg_type;

/* Every page type the app knows about. To add a new kind of page, write
 * src/pages/<name>_page.c exporting a PageType and list it here. */
static const PageType *const TYPES[] = {
    &clock_type,
    &weather_type,
    &svg_type,
};

static const PageType *find_type(const char *name)
{
    for (gsize i = 0; i < G_N_ELEMENTS(TYPES); i++)
        if (strcmp(TYPES[i]->name, name) == 0)
            return TYPES[i];
    return NULL;
}

GPtrArray *pages_load(GKeyFile *cfg)
{
    GPtrArray *pages = g_ptr_array_new();
    gsize n = 0;
    char **sections = g_key_file_get_string_list(cfg, "app", "pages", &n, NULL);

    for (gsize i = 0; i < n; i++) {
        const char *sec = g_strstrip(sections[i]);
        if (!*sec)
            continue;
        if (!g_key_file_has_group(cfg, sec)) {
            g_warning("pages= lists [%s] but there is no such section", sec);
            continue;
        }
        char *tname = cfg_string(cfg, sec, "type", NULL);
        const PageType *type = tname ? find_type(tname) : NULL;
        if (!type) {
            g_warning("[%s]: unknown type '%s'", sec, tname ? tname : "(missing)");
            g_free(tname);
            continue;
        }
        g_free(tname);

        Page *p = g_new0(Page, 1);
        p->type = type;
        p->section = g_strdup(sec);
        p->title = cfg_string(cfg, sec, "title", "");
        p->index = pages->len;
        if (type->init)
            type->init(p, cfg, sec);
        g_ptr_array_add(pages, p);
    }

    g_strfreev(sections);
    return pages;
}
