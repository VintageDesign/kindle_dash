#include "ui.h"

#include <math.h>
#include <pango/pangocairo.h>

static char *font_family;

void ui_set_font_family(const char *family)
{
    g_free(font_family);
    font_family = g_strdup(family && *family ? family : "Sans");
}

Rect ui_rect(double x, double y, double w, double h)
{
    Rect r = { x, y, w, h };
    return r;
}

Rect ui_inset(Rect r, double d)
{
    return ui_rect(r.x + d, r.y + d, MAX(0, r.w - 2 * d), MAX(0, r.h - 2 * d));
}

Rect ui_take_top(Rect *r, double h)
{
    h = MIN(h, r->h);
    Rect top = ui_rect(r->x, r->y, r->w, h);
    r->y += h;
    r->h -= h;
    return top;
}

Rect ui_take_bottom(Rect *r, double h)
{
    h = MIN(h, r->h);
    r->h -= h;
    return ui_rect(r->x, r->y + r->h, r->w, h);
}

void ui_split(Rect r, int n, gboolean vertical, double gap, Rect *out)
{
    if (n <= 0)
        return;
    if (vertical) {
        double h = (r.h - gap * (n - 1)) / n;
        for (int i = 0; i < n; i++)
            out[i] = ui_rect(r.x, r.y + i * (h + gap), r.w, h);
    } else {
        double w = (r.w - gap * (n - 1)) / n;
        for (int i = 0; i < n; i++)
            out[i] = ui_rect(r.x + i * (w + gap), r.y, w, r.h);
    }
}

void ui_grid(Rect r, int rows, int cols, double gap, Rect *out)
{
    Rect row[rows];
    ui_split(r, rows, TRUE, gap, row);
    for (int i = 0; i < rows; i++)
        ui_split(row[i], cols, FALSE, gap, out + i * cols);
}

static PangoLayout *make_layout(cairo_t *cr, const char *text, gboolean bold, double px, double wrap_w)
{
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, font_family ? font_family : "Sans");
    pango_font_description_set_weight(fd, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    if (wrap_w > 0) {
        pango_layout_set_width(layout, wrap_w * PANGO_SCALE);
        pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    }
    pango_layout_set_text(layout, text, -1);
    return layout;
}

static double text_fit(cairo_t *cr, Rect r, const char *text, gboolean bold, double max_px,
                       UiAlign align, gboolean wrap)
{
    double wrap_w = wrap ? r.w : 0;
    if (!text || !*text || r.w < 1 || r.h < 1)
        return 0;

    /* Binary search for the largest size whose logical extents fit. */
    double lo = 4, hi = MAX(lo, MIN(max_px, r.h * 1.2));
    int tw, th;
    for (int i = 0; i < 14 && hi - lo > 0.5; i++) {
        double mid = (lo + hi) / 2;
        PangoLayout *l = make_layout(cr, text, bold, mid, wrap_w);
        pango_layout_get_pixel_size(l, &tw, &th);
        g_object_unref(l);
        if (tw <= r.w && th <= r.h)
            lo = mid;
        else
            hi = mid;
    }

    PangoLayout *l = make_layout(cr, text, bold, floor(lo), wrap_w);
    if (wrap)
        pango_layout_set_alignment(l, align == UI_CENTER ? PANGO_ALIGN_CENTER
                                      : align == UI_RIGHT ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT);
    pango_layout_get_pixel_size(l, &tw, &th);
    double x = r.x;
    if (wrap)
        tw = r.w; /* alignment is handled by pango inside the wrap width */
    if (align == UI_CENTER)
        x = r.x + (r.w - tw) / 2;
    else if (align == UI_RIGHT)
        x = r.x + r.w - tw;
    cairo_move_to(cr, x, r.y + (r.h - th) / 2);
    pango_cairo_show_layout(cr, l);
    g_object_unref(l);
    return floor(lo);
}

double ui_text_fit(cairo_t *cr, Rect r, const char *text, gboolean bold, double max_px, UiAlign align)
{
    return text_fit(cr, r, text, bold, max_px, align, FALSE);
}

double ui_text_wrap(cairo_t *cr, Rect r, const char *text, gboolean bold, double max_px, UiAlign align)
{
    return text_fit(cr, r, text, bold, max_px, align, TRUE);
}

void ui_progress_bar(cairo_t *cr, Rect r, double fraction, double line_w)
{
    fraction = CLAMP(fraction, 0.0, 1.0);
    cairo_set_line_width(cr, line_w);
    cairo_rectangle(cr, r.x + line_w / 2, r.y + line_w / 2, r.w - line_w, r.h - line_w);
    cairo_stroke(cr);
    Rect in = ui_inset(r, line_w * 2);
    if (fraction > 0) {
        cairo_rectangle(cr, in.x, in.y, in.w * fraction, in.h);
        cairo_fill(cr);
    }
}

void ui_hline(cairo_t *cr, double x0, double x1, double y, double line_w)
{
    cairo_set_line_width(cr, line_w);
    cairo_move_to(cr, x0, y);
    cairo_line_to(cr, x1, y);
    cairo_stroke(cr);
}

void ui_vline(cairo_t *cr, double x, double y0, double y1, double line_w)
{
    cairo_set_line_width(cr, line_w);
    cairo_move_to(cr, x, y0);
    cairo_line_to(cr, x, y1);
    cairo_stroke(cr);
}

void ui_header(cairo_t *cr, Rect r, const char *title, double line_w)
{
    ui_text_fit(cr, ui_inset(r, r.h * 0.15), title, TRUE, r.h, UI_CENTER);
    ui_hline(cr, r.x, r.x + r.w, r.y + r.h - line_w / 2, line_w);
}

void ui_footer(cairo_t *cr, Rect r, int page_idx, int n_pages)
{
    if (n_pages < 2)
        return;
    double rad = r.h * 0.18;
    double step = rad * 4;
    double x = r.x + r.w / 2 - step * (n_pages - 1) / 2;
    double y = r.y + r.h / 2;
    cairo_set_line_width(cr, MAX(1, rad * 0.35));
    for (int i = 0; i < n_pages; i++) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + i * step, y, rad, 0, 2 * G_PI);
        if (i == page_idx)
            cairo_fill(cr);
        else
            cairo_stroke(cr);
    }
}
