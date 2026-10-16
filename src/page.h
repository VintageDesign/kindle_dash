#ifndef KD_PAGE_H
#define KD_PAGE_H

#include <glib.h>
#include <cairo.h>

typedef struct Page Page;

/* A PageType is the code for one kind of page. Each [page:*] section in
 * dash.ini creates one Page instance of a type, so the same type can appear
 * several times with different settings (e.g. two Claude accounts). */
typedef struct {
    const char *name;                                         /* value of type= in dash.ini */
    void (*init)(Page *p, GKeyFile *cfg, const char *section); /* read own section, alloc state */
    void (*draw)(Page *p, cairo_t *cr, int w, int h);          /* content area only, origin 0,0 */
    void (*on_show)(Page *p);                                  /* optional */
    void (*on_hide)(Page *p);                                  /* optional */
    guint redraw_interval_s;                                   /* redraw while visible; 0 = only on request */
} PageType;

struct Page {
    const PageType *type;
    char *section;  /* e.g. "page:weather" */
    char *title;    /* drawn by the app as the page header; may be empty */
    int index;      /* position in the cycle */
    void *state;    /* owned by the page type */
};

/* Ask the app to redraw p. Ignored unless p is the visible page. */
void app_request_redraw(Page *p);

/* Path of the directory holding dash.ini; relative paths in the config resolve against it. */
const char *app_config_dir(void);

#endif
