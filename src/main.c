#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "http_json.h"
#include "pages.h"
#include "ui.h"

/* The Kindle window manager reads placement hints from the window title:
 * L:A = application layer, PC:N = no chrome (fullscreen). */
#define KINDLE_WINDOW_TITLE "L:A_N:application_ID:net.riley.kindledash_PC:N"

/* [app] orientation=: the app rotates its own drawing (and taps) rather than
 * asking the Kindle window manager, so nothing system-wide changes. */
typedef enum {
  ORIENT_AUTO,
  ORIENT_LANDSCAPE,
  ORIENT_LANDSCAPE_LEFT,
  ORIENT_PORTRAIT
} Orientation;

static Orientation parse_orientation(const char *name) {
  if (!name || g_ascii_strcasecmp(name, "landscape") == 0)
    return ORIENT_LANDSCAPE;
  if (g_ascii_strcasecmp(name, "landscape-left") == 0)
    return ORIENT_LANDSCAPE_LEFT;
  if (g_ascii_strcasecmp(name, "portrait") == 0)
    return ORIENT_PORTRAIT;
  return ORIENT_AUTO;
}

static struct {
  GtkWidget *window;
  GtkWidget *area;
  GPtrArray *pages;
  guint current;
  int cycle_s;
  Orientation orientation;
  time_t last_switch;
  time_t last_tick;
  char *config_dir;
  char *fbink;           /* path to fbink, NULL if unavailable */
  gboolean full_refresh; /* flash the screen after the next draw */
  int corner_taps;       /* exit gesture */
  time_t corner_first;
  gboolean show_headers;
} app;

static volatile sig_atomic_t
    quit_signal; /* signal number that asked us to quit */

const char *app_config_dir(void) { return app.config_dir; }

static Page *current_page(void) {
  if (!app.pages || app.pages->len == 0)
    return NULL;
  return g_ptr_array_index(app.pages, app.current);
}

void app_request_redraw(Page *p) {
  if (p == current_page())
    gtk_widget_queue_draw(app.area);
}

static void run_quiet(const char *const *argv) {
  g_spawn_async(NULL, (char **)argv, NULL,
                G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                    G_SPAWN_STDERR_TO_DEV_NULL,
                NULL, NULL, NULL, NULL);
}

static void prevent_screensaver(gboolean on) {
  char *lipc = g_find_program_in_path("lipc-set-prop");
  if (!lipc)
    return;
  const char *argv[] = {lipc, "com.lab126.powerd", "preventScreenSaver",
                        on ? "1" : "0", NULL};
  run_quiet(argv);
  g_free(lipc);
}

/* Degrees clockwise to rotate the content for a window of this size: 0, 90 or
 * 270. */
static int content_rotation(int win_w, int win_h) {
  gboolean win_landscape = win_w > win_h;
  switch (app.orientation) {
  case ORIENT_LANDSCAPE:
    return win_landscape ? 0 : 90;
  case ORIENT_LANDSCAPE_LEFT:
    return win_landscape ? 0 : 270;
  case ORIENT_PORTRAIT:
    return win_landscape ? 90 : 0;
  default:
    return 0;
  }
}

static gboolean do_full_refresh(gpointer unused) {
  /* Full flashing refresh to clear e-ink ghosting after a page change. */
  const char *argv[] = {app.fbink, "-q", "-f", "-s", NULL};
  run_quiet(argv);
  return FALSE;
}

static void switch_page(int delta) {
  guint n = app.pages->len;
  if (n == 0)
    return;
  Page *old = current_page();
  app.current = (app.current + n + delta) % n;
  Page *p = current_page();
  if (old != p) {
    if (old->type->on_hide)
      old->type->on_hide(old);
    if (p->type->on_show)
      p->type->on_show(p);
    app.full_refresh = app.fbink != NULL;
  }
  app.last_switch = time(NULL);
  gtk_widget_queue_draw(app.area);
}

/* Draw the whole screen: header, current page, page dots. */
static void draw_frame(cairo_t *cr, int width, int height) {
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_paint(cr);
  cairo_set_source_rgb(cr, 0, 0, 0);

  Page *p = current_page();
  if (!p) {
    ui_text_wrap(cr, ui_inset(ui_rect(0, 0, width, height), width * 0.1),
                 "No pages configured - check [app] pages= in dash.ini", FALSE,
                 60, UI_CENTER);
    return;
  }

  double u = ui_unit(width, height);
  Rect body = ui_inset(ui_rect(0, 0, width, height), u * 2);
  Rect footer = ui_take_bottom(&body, u * 5);
  if (p->title && *p->title && app.show_headers) {
    Rect header = ui_take_top(&body, u * 10);
    ui_header(cr, header, p->title, MAX(2, u * 0.5));
    ui_take_top(&body, u * 2);
  }
  ui_footer(cr, footer, app.current, app.pages->len);

  cairo_save(cr);
  cairo_rectangle(cr, body.x, body.y, body.w, body.h);
  cairo_clip(cr);
  cairo_translate(cr, body.x, body.y);
  p->type->draw(p, cr, (int)body.w, (int)body.h);
  cairo_restore(cr);
}

/* Draw into a win_w x win_h surface, rotating the content if the orientation
 * asks for it. */
static void draw_screen(cairo_t *cr, int win_w, int win_h) {
  switch (content_rotation(win_w, win_h)) {
  case 90:
    cairo_translate(cr, win_w, 0);
    cairo_rotate(cr, G_PI / 2);
    draw_frame(cr, win_h, win_w);
    break;
  case 270:
    cairo_translate(cr, 0, win_h);
    cairo_rotate(cr, -G_PI / 2);
    draw_frame(cr, win_h, win_w);
    break;
  default:
    draw_frame(cr, win_w, win_h);
  }
}

static gboolean on_expose(GtkWidget *w, GdkEventExpose *ev, gpointer unused) {
  GtkAllocation a;
  gtk_widget_get_allocation(w, &a);
  cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(w));
  draw_screen(cr, a.width, a.height);
  cairo_destroy(cr);

  if (app.full_refresh) {
    app.full_refresh = FALSE;
    g_timeout_add(300, do_full_refresh, NULL);
  }
  return TRUE;
}

#ifdef KINDLEDASH_HOST
/* Host-only test hook: KINDLEDASH_RENDER=prefix renders every page at
 * KINDLEDASH_SIZE to prefix-<n>.png after a few seconds (so fetches land), then
 * exits. */
static gboolean render_to_png(gpointer prefix) {
  int w = 1072, h = 1448;
  const char *sz = getenv("KINDLEDASH_SIZE");
  if (sz)
    sscanf(sz, "%dx%d", &w, &h);
  for (guint i = 0; i < app.pages->len; i++) {
    app.current = i;
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t *cr = cairo_create(s);
    draw_screen(cr, w, h);
    cairo_destroy(cr);
    char *path = g_strdup_printf("%s-%u.png", (char *)prefix, i);
    cairo_surface_write_to_png(s, path);
    g_free(path);
    cairo_surface_destroy(s);
  }
  gtk_main_quit();
  return FALSE;
}
#endif

static gboolean on_configure(GtkWidget *w, GdkEventConfigure *ev,
                             gpointer unused) {
  static int last_w, last_h;
  if (ev->width != last_w || ev->height != last_h) {
    g_message("window is %dx%d", ev->width, ev->height);
    last_w = ev->width;
    last_h = ev->height;
  }
  /* Rotation or resize: pages lay out from the new size on the next draw. */
  gtk_widget_queue_draw(app.area);
  return FALSE;
}

static gboolean on_visibility(GtkWidget *w, GdkEventVisibility *ev,
                              gpointer unused) {
  static const char *names[] = {"visible", "partially covered",
                                "fully covered"};
  if (ev->state >= 0 && ev->state < 3)
    g_message("window %s", names[ev->state]);
  if (ev->state != GDK_VISIBILITY_FULLY_OBSCURED)
    gtk_widget_queue_draw(app.area);
  return FALSE;
}

static gboolean on_button(GtkWidget *w, GdkEventButton *ev, gpointer unused) {
  if (ev->type != GDK_BUTTON_PRESS)
    return TRUE;
  GtkAllocation a;
  gtk_widget_get_allocation(w, &a);

  /* Map the tap into the (possibly rotated) content's coordinates. */
  double x = ev->x, y = ev->y, cw = a.width, ch = a.height;
  switch (content_rotation(a.width, a.height)) {
  case 90:
    x = ev->y;
    y = a.width - ev->x;
    cw = a.height;
    ch = a.width;
    break;
  case 270:
    x = a.height - ev->y;
    y = ev->x;
    cw = a.height;
    ch = a.width;
    break;
  }

  double corner = MIN(cw, ch) * 0.15;
  time_t now = time(NULL);

  /* Three taps in the content's top-left corner within 3 seconds quits. */
  if (x < corner && y < corner) {
    if (app.corner_taps == 0 || now - app.corner_first > 3) {
      app.corner_taps = 0;
      app.corner_first = now;
    }
    if (++app.corner_taps >= 3)
      gtk_main_quit();
    return TRUE;
  }
  app.corner_taps = 0;

  switch_page(x < cw / 3.0 ? -1 : 1);
  return TRUE;
}

static gboolean on_tick(gpointer unused) {
  if (quit_signal) {
    g_message("received signal %d, quitting", (int)quit_signal);
    gtk_main_quit();
    return FALSE;
  }
  time_t now = time(NULL);
  if (app.cycle_s > 0 && app.pages->len > 1 &&
      now - app.last_switch >= app.cycle_s) {
    switch_page(1);
  } else {
    Page *p = current_page();
    guint iv = p ? p->type->redraw_interval_s : 0;
    /* Redraw when we cross an interval boundary, e.g. each new minute. */
    if (iv > 0 && now / iv != app.last_tick / iv)
      gtk_widget_queue_draw(app.area);
  }
  app.last_tick = now;
  return TRUE;
}

static void on_signal(int sig) { quit_signal = sig; }

static void on_destroy(GtkWidget *w, gpointer unused) { gtk_main_quit(); }

int main(int argc, char **argv) {
  gtk_init(&argc, &argv);

  const char *config_path = argc > 1 ? argv[1] : "dash.ini";
  GKeyFile *cfg = g_key_file_new();
  GError *err = NULL;
  if (!g_key_file_load_from_file(cfg, config_path, G_KEY_FILE_NONE, &err)) {
    g_printerr("kindledash: cannot load %s: %s\n", config_path, err->message);
    return 1;
  }
  char *abs_config =
      g_path_is_absolute(config_path)
          ? g_strdup(config_path)
          : g_build_filename(g_get_current_dir(), config_path, NULL);
  app.config_dir = g_path_get_dirname(abs_config);
  g_free(abs_config);

  app.cycle_s = cfg_int(cfg, "app", "cycle_seconds", 30);
  char *font = cfg_string(cfg, "app", "font", "Sans");
  ui_set_font_family(font);
  g_free(font);
  char *ca = cfg_path(cfg, "app", "ca_bundle", NULL);
  http_json_set_ca_bundle(ca);
  g_free(ca);

  char *orientation = cfg_string(cfg, "app", "orientation", "landscape");
  app.orientation = parse_orientation(orientation);
  g_free(orientation);

  app.show_headers = cfg_bool(cfg, "app", "show_headers", TRUE);

  char *fbink = cfg_path(cfg, "app", "fbink", "/mnt/us/libkh/bin/fbink");
  if (fbink && g_file_test(fbink, G_FILE_TEST_IS_EXECUTABLE))
    app.fbink = fbink;
  else {
    g_free(fbink);
    app.fbink = g_find_program_in_path("fbink");
  }

  g_message("starting (pid %d, config %s)", (int)getpid(), config_path);
  app.pages = pages_load(cfg);
  g_message("%u page(s) loaded", app.pages->len);
  g_key_file_free(cfg);

  app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(app.window), KINDLE_WINDOW_TITLE);
#ifdef KINDLEDASH_HOST
  {
    /* Default: half the PW4's 1072x1448. KINDLEDASH_SIZE=WxH overrides, e.g.
     * 724x536 for landscape. */
    int ww = 536, wh = 724;
    const char *sz = getenv("KINDLEDASH_SIZE");
    if (sz)
      sscanf(sz, "%dx%d", &ww, &wh);
    gtk_window_set_default_size(GTK_WINDOW(app.window), ww, wh);
  }
#endif
  app.area = gtk_drawing_area_new();
  gtk_widget_add_events(app.area, GDK_BUTTON_PRESS_MASK);
  gtk_container_add(GTK_CONTAINER(app.window), app.area);

  g_signal_connect(app.window, "destroy", G_CALLBACK(on_destroy), NULL);
  g_signal_connect(app.window, "configure-event", G_CALLBACK(on_configure),
                   NULL);
  gtk_widget_add_events(app.window, GDK_VISIBILITY_NOTIFY_MASK);
  g_signal_connect(app.window, "visibility-notify-event",
                   G_CALLBACK(on_visibility), NULL);
  g_signal_connect(app.area, "expose-event", G_CALLBACK(on_expose), NULL);
  g_signal_connect(app.area, "button-press-event", G_CALLBACK(on_button), NULL);

  signal(SIGTERM, on_signal);
  signal(SIGINT, on_signal);
  signal(SIGHUP, on_signal);

  Page *first = current_page();
  if (first && first->type->on_show)
    first->type->on_show(first);
  app.last_switch = app.last_tick = time(NULL);
  g_timeout_add_seconds(1, on_tick, NULL);

#ifdef KINDLEDASH_HOST
  if (getenv("KINDLEDASH_RENDER")) {
    g_timeout_add_seconds(5, render_to_png,
                          (gpointer)getenv("KINDLEDASH_RENDER"));
    gtk_main();
    return 0;
  }
#endif

  prevent_screensaver(TRUE);
  gtk_widget_show_all(app.window);
  gtk_main();
  prevent_screensaver(FALSE);
  g_message("exiting");
  /* Report a signal-requested quit as 128+sig, like a shell would. */
  return quit_signal ? 128 + quit_signal : 0;
}
