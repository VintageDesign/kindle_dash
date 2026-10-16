#include <math.h>
#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>

#include "../config.h"
#include "../page.h"
#include "../ui.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#define NANOSVG_IMPLEMENTATION
#include "../../third_party/nanosvg.h"
#pragma GCC diagnostic pop

/* Draws an SVG file scaled to the page. The file is re-read when it changes, so
 * another program can keep it up to date. Config:
 *   path=image.svg      (relative to dash.ini's directory)
 *   fit=contain         (keep aspect ratio) or stretch (fill the whole page)
 *   margin=2            (in 1%-of-short-side units)
 *   poll_seconds=10     (how often to check the file for changes; 0 = load once, never reload)
 * nanosvg does not support <text>, <image>, filters or masks; convert text to paths. */

typedef struct {
    Page *page;
    char *path;
    gboolean stretch;
    double margin;
    int poll_s;          /* 0 = never reload */

    NSVGimage *img;  /* last image that loaded; kept if a reload fails */
    char *error;     /* shown when there is no image */
    gboolean missing;
    gboolean have_stat;
    time_t mtime;
    goffset size;
} SvgState;

static void set_error(SvgState *st, char *msg)
{
    g_free(st->error);
    st->error = msg;
}

/* Re-read the file if its mtime or size changed. Returns TRUE if the page should redraw. */
static gboolean load_if_changed(SvgState *st)
{
    GStatBuf sb;
    if (g_stat(st->path, &sb) != 0) {
        if (st->missing)
            return FALSE;
        st->missing = TRUE;
        st->have_stat = FALSE;
        g_warning("svg: cannot read %s", st->path);
        if (st->img)
            return FALSE;
        set_error(st, g_strdup_printf("Cannot read %s", st->path));
        return TRUE;
    }
    st->missing = FALSE;
    if (st->have_stat && sb.st_mtime == st->mtime && sb.st_size == st->size)
        return FALSE;
    st->have_stat = TRUE;
    st->mtime = sb.st_mtime;
    st->size = sb.st_size;

    NSVGimage *img = nsvgParseFromFile(st->path, "px", 96);
    if (!img || img->width <= 0 || img->height <= 0) {
        if (img)
            nsvgDelete(img);
        g_warning("svg: %s is not a valid SVG (needs width/height or viewBox)", st->path);
        if (st->img)
            return FALSE;
        set_error(st, g_strdup_printf("%s is not a valid SVG", st->path));
        return TRUE;
    }
    if (st->img)
        nsvgDelete(st->img);
    st->img = img;
    set_error(st, NULL);
    return TRUE;
}

static gboolean on_poll(gpointer data)
{
    SvgState *st = data;
    if (load_if_changed(st))
        app_request_redraw(st->page);
    return TRUE;
}

static void svg_init(Page *p, GKeyFile *cfg, const char *section)
{
    SvgState *st = g_new0(SvgState, 1);
    st->page = p;
    st->path = cfg_path(cfg, section, "path", "image.svg");
    char *fit = cfg_string(cfg, section, "fit", "contain");
    st->stretch = g_ascii_strcasecmp(fit, "stretch") == 0;
    g_free(fit);
    st->margin = MAX(0, cfg_int(cfg, section, "margin", 2));
    st->poll_s = MAX(0, cfg_int(cfg, section, "poll_seconds", 10));
    p->state = st;
    load_if_changed(st);
    if (st->poll_s > 0)
        g_timeout_add_seconds(st->poll_s, on_poll, st);
}

static void svg_on_show(Page *p)
{
    SvgState *st = p->state;
    if (st->poll_s > 0)
        load_if_changed(st);
}

/* --- Drawing --- */

/* nanosvg packs colors as 0xAABBGGRR. */
static void color_stop(cairo_pattern_t *pat, double offset, unsigned int c, double opacity)
{
    cairo_pattern_add_color_stop_rgba(pat, offset, (c & 0xff) / 255.0, ((c >> 8) & 0xff) / 255.0,
                                      ((c >> 16) & 0xff) / 255.0, ((c >> 24) & 0xff) / 255.0 * opacity);
}

/* Set the source to a fill or stroke paint. Returns FALSE for "none". */
static gboolean set_paint(cairo_t *cr, const NSVGpaint *paint, double opacity)
{
    if (paint->type == NSVG_PAINT_COLOR) {
        unsigned int c = paint->color;
        cairo_set_source_rgba(cr, (c & 0xff) / 255.0, ((c >> 8) & 0xff) / 255.0,
                              ((c >> 16) & 0xff) / 255.0, ((c >> 24) & 0xff) / 255.0 * opacity);
        return TRUE;
    }
    if (paint->type != NSVG_PAINT_LINEAR_GRADIENT && paint->type != NSVG_PAINT_RADIAL_GRADIENT)
        return FALSE;

    /* g->xform maps user space to a unit gradient space: linear runs along y
     * from 0 to 1, radial is centered on 0,0 with radius 1. nanosvg's focal
     * point (fx/fy) is not usable, so radials are drawn from the center. */
    const NSVGgradient *g = paint->gradient;
    cairo_pattern_t *pat = paint->type == NSVG_PAINT_LINEAR_GRADIENT
        ? cairo_pattern_create_linear(0, 0, 0, 1)
        : cairo_pattern_create_radial(0, 0, 0, 0, 0, 1);
    for (int i = 0; i < g->nstops; i++)
        color_stop(pat, g->stops[i].offset, g->stops[i].color, opacity);
    cairo_matrix_t m;
    cairo_matrix_init(&m, g->xform[0], g->xform[1], g->xform[2], g->xform[3], g->xform[4], g->xform[5]);
    cairo_pattern_set_matrix(pat, &m);
    cairo_pattern_set_extend(pat, g->spread == NSVG_SPREAD_REFLECT ? CAIRO_EXTEND_REFLECT
                                : g->spread == NSVG_SPREAD_REPEAT ? CAIRO_EXTEND_REPEAT
                                : CAIRO_EXTEND_PAD);
    cairo_set_source(cr, pat);
    cairo_pattern_destroy(pat);
    return TRUE;
}

static void shape_path(cairo_t *cr, const NSVGshape *shape)
{
    cairo_new_path(cr);
    for (const NSVGpath *path = shape->paths; path; path = path->next) {
        const float *pt = path->pts;
        cairo_move_to(cr, pt[0], pt[1]);
        for (int i = 0; i < path->npts - 1; i += 3) {
            const float *c = &pt[i * 2];
            cairo_curve_to(cr, c[2], c[3], c[4], c[5], c[6], c[7]);
        }
        if (path->closed)
            cairo_close_path(cr);
    }
}

static void fill_shape(cairo_t *cr, const NSVGshape *shape)
{
    if (!set_paint(cr, &shape->fill, shape->opacity))
        return;
    cairo_set_fill_rule(cr, shape->fillRule == NSVG_FILLRULE_EVENODD ? CAIRO_FILL_RULE_EVEN_ODD
                                                                       : CAIRO_FILL_RULE_WINDING);
    cairo_fill_preserve(cr);
}

static void stroke_shape(cairo_t *cr, const NSVGshape *shape)
{
    if (shape->strokeWidth <= 0 || !set_paint(cr, &shape->stroke, shape->opacity))
        return;
    cairo_set_line_width(cr, shape->strokeWidth);
    cairo_set_line_cap(cr, shape->strokeLineCap == NSVG_CAP_ROUND ? CAIRO_LINE_CAP_ROUND
                           : shape->strokeLineCap == NSVG_CAP_SQUARE ? CAIRO_LINE_CAP_SQUARE
                           : CAIRO_LINE_CAP_BUTT);
    cairo_set_line_join(cr, shape->strokeLineJoin == NSVG_JOIN_ROUND ? CAIRO_LINE_JOIN_ROUND
                            : shape->strokeLineJoin == NSVG_JOIN_BEVEL ? CAIRO_LINE_JOIN_BEVEL
                            : CAIRO_LINE_JOIN_MITER);
    cairo_set_miter_limit(cr, shape->miterLimit);
    double dashes[8];
    int n = MIN(shape->strokeDashCount, 8);
    for (int i = 0; i < n; i++)
        dashes[i] = shape->strokeDashArray[i];
    cairo_set_dash(cr, dashes, n, shape->strokeDashOffset);
    cairo_stroke_preserve(cr);
}

static void svg_draw(Page *p, cairo_t *cr, int w, int h)
{
    SvgState *st = p->state;
    if (!st->img) {
        ui_text_wrap(cr, ui_inset(ui_rect(0, 0, w, h), w * 0.1),
                     st->error ? st->error : "No image", FALSE, h * 0.05, UI_CENTER);
        return;
    }

    Rect r = ui_inset(ui_rect(0, 0, w, h), st->margin * ui_unit(w, h));
    double sx = r.w / st->img->width, sy = r.h / st->img->height;
    if (!st->stretch)
        sx = sy = MIN(sx, sy);

    cairo_save(cr);
    cairo_translate(cr, r.x + (r.w - st->img->width * sx) / 2, r.y + (r.h - st->img->height * sy) / 2);
    cairo_scale(cr, sx, sy);
    for (const NSVGshape *shape = st->img->shapes; shape; shape = shape->next) {
        if (!(shape->flags & NSVG_FLAGS_VISIBLE))
            continue;
        shape_path(cr, shape);
        /* paintOrder holds three 2-bit steps; markers are not supported. */
        for (int i = 0; i < 3; i++) {
            int step = (shape->paintOrder >> (i * 2)) & 0x03;
            if (step == NSVG_PAINT_FILL)
                fill_shape(cr, shape);
            else if (step == NSVG_PAINT_STROKE)
                stroke_shape(cr, shape);
        }
    }
    cairo_new_path(cr);
    cairo_restore(cr);
}

const PageType svg_type = {
    .name = "svg",
    .init = svg_init,
    .draw = svg_draw,
    .on_show = svg_on_show,
    .redraw_interval_s = 0,
};
