#ifndef KD_UI_H
#define KD_UI_H

#include <glib.h>
#include <cairo.h>

/* Layout helpers. Pages size everything from the w/h they are given so the
 * same code works in portrait and landscape. */

typedef struct {
    double x, y, w, h;
} Rect;

typedef enum { UI_LEFT, UI_CENTER, UI_RIGHT } UiAlign;

void ui_set_font_family(const char *family);

static inline gboolean ui_is_landscape(int w, int h) { return w > h; }
/* 1% of the short side: use multiples of this for margins and line widths. */
static inline double ui_unit(int w, int h) { return (w < h ? w : h) / 100.0; }

Rect ui_rect(double x, double y, double w, double h);
Rect ui_inset(Rect r, double d);
/* Cut a strip off the top/bottom of *r and return it; *r shrinks accordingly. */
Rect ui_take_top(Rect *r, double h);
Rect ui_take_bottom(Rect *r, double h);
/* Split r into n equal parts with gap between them, stacked vertically or side by side. */
void ui_split(Rect r, int n, gboolean vertical, double gap, Rect *out);
/* Split r into a rows x cols grid, filled row-major. */
void ui_grid(Rect r, int rows, int cols, double gap, Rect *out);

/* Draw text at the largest size (<= max_px) that fits in r, vertically centered.
 * Returns the pixel size used. */
double ui_text_fit(cairo_t *cr, Rect r, const char *text, gboolean bold, double max_px, UiAlign align);

/* Like ui_text_fit, but word-wraps to the width of r. For long messages. */
double ui_text_wrap(cairo_t *cr, Rect r, const char *text, gboolean bold, double max_px, UiAlign align);

void ui_progress_bar(cairo_t *cr, Rect r, double fraction, double line_w);
void ui_hline(cairo_t *cr, double x0, double x1, double y, double line_w);
void ui_vline(cairo_t *cr, double x, double y0, double y1, double line_w);

void ui_header(cairo_t *cr, Rect r, const char *title, double line_w);
void ui_footer(cairo_t *cr, Rect r, int page_idx, int n_pages);

#endif
