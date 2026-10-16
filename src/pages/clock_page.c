#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../config.h"
#include "../page.h"
#include "../ui.h"

/* World clock. Config:
 *   cities=Label|POSIX-TZ;Label|POSIX-TZ
 *   hour24=true
 * POSIX TZ strings (e.g. CET-1CEST,M3.5.0,M10.5.0/3) are used instead of
 * zoneinfo names because the Kindle's tzdata may be missing or out of date. */

typedef struct {
  char *label;
  char *tz;
} City;

typedef struct {
  GPtrArray *cities;
  gboolean hour24;
} ClockState;

static void clock_init(Page *p, GKeyFile *cfg, const char *section) {
  ClockState *st = g_new0(ClockState, 1);
  st->cities = g_ptr_array_new();
  st->hour24 = cfg_bool(cfg, section, "hour24", TRUE);

  gsize n = 0;
  char **items = g_key_file_get_string_list(cfg, section, "cities", &n, NULL);
  for (gsize i = 0; i < n; i++) {
    char *bar = strchr(items[i], '|');
    if (!bar) {
      g_warning("[%s] cities: '%s' should be Label|TZ", section, items[i]);
      continue;
    }
    *bar = '\0';
    City *c = g_new0(City, 1);
    c->label = g_strdup(g_strstrip(items[i]));
    c->tz = g_strdup(g_strstrip(bar + 1));
    g_ptr_array_add(st->cities, c);
  }
  g_strfreev(items);
  p->state = st;
}

/* localtime in another zone. Single-threaded, so swapping TZ is safe. */
static void time_in_zone(time_t now, const char *tz, struct tm *out) {
  char *saved = g_strdup(getenv("TZ"));
  setenv("TZ", tz, 1);
  tzset();
  localtime_r(&now, out);
  if (saved)
    setenv("TZ", saved, 1);
  else
    unsetenv("TZ");
  tzset();
  g_free(saved);
}

static void draw_city(cairo_t *cr, Rect cell, const City *c, time_t now,
                      gboolean hour24) {
  struct tm tm;
  time_in_zone(now, c->tz, &tm);
  gboolean is_utc = strcmp(c->tz, "UTC0") == 0;

  char time_s[32], ampm[8] = "", zone_s[64];
  if (hour24) {
    strftime(time_s, sizeof time_s, "%H:%M", &tm);
  } else {
    strftime(time_s, sizeof time_s, "%l:%M", &tm);
    strftime(ampm, sizeof ampm, "%p", &tm);
  }

  long off = tm.tm_gmtoff / 60;
  if (off % 60)
    g_snprintf(zone_s, sizeof zone_s, "%s  ·  UTC%+ld:%02ld", tm.tm_zone,
               off / 60, labs(off % 60));
  else
    g_snprintf(zone_s, sizeof zone_s, "%s  ·  UTC%+ld", tm.tm_zone, off / 60);

  Rect r = ui_inset(cell, MIN(cell.w, cell.h) * 0.04);
  Rect label = ui_take_top(&r, r.h * 0.18);
  Rect zone = ui_take_bottom(&r, r.h * 0.14);

  ui_text_fit(cr, label, c->label, TRUE, label.h, UI_CENTER);
  if (*ampm) {
    char *t = g_strdup_printf("%s %s", g_strstrip(time_s), ampm);
    ui_text_fit(cr, r, t, TRUE, r.h, UI_CENTER);
    g_free(t);
  } else {
    ui_text_fit(cr, r, time_s, TRUE, r.h, UI_CENTER);
  }
  if (!is_utc) {
    ui_text_fit(cr, zone, zone_s, FALSE, zone.h * 0.7, UI_CENTER);
  }
}

static void clock_draw(Page *p, cairo_t *cr, int w, int h) {
  ClockState *st = p->state;
  int n = st->cities->len;
  if (n == 0) {
    ui_text_fit(cr, ui_rect(0, 0, w, h), "No cities configured", FALSE,
                h * 0.05, UI_CENTER);
    return;
  }

  time_t now = time(NULL);

  char date_s[64];
  double line = ui_unit(w, h) * 0.4;

  struct tm tm;
  time_in_zone(now, ((const City *)g_ptr_array_index(st->cities, 0))->tz, &tm);
  strftime(date_s, sizeof date_s, "%A, %d %B", &tm);

  Rect body = ui_rect(0, 0, w, h);
  Rect date_zone = ui_take_top(&body, h * .08);
  ui_text_fit(cr, date_zone, date_s, FALSE, date_zone.h * .8, UI_CENTER);

  /* Stack cities in portrait, put them side by side in landscape. */
  gboolean vertical = !ui_is_landscape(w, h);
  Rect cells[n];
  ui_split(body, n, vertical, 0, cells);

  for (int i = 0; i < n; i++) {
    draw_city(cr, cells[i], g_ptr_array_index(st->cities, i), now, st->hour24);
    if (i > 0) {
      if (vertical)
        ui_hline(cr, w * 0.1, w * 0.9, cells[i].y, line);
      else
        ui_vline(cr, cells[i].x, h * 0.1, h * 0.9, line);
    }
  }
}

const PageType clock_type = {
    .name = "clock",
    .init = clock_init,
    .draw = clock_draw,
    .redraw_interval_s = 60,
};
