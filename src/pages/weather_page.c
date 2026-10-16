#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../config.h"
#include "../http_json.h"
#include "../page.h"
#include "../ui.h"

/* Current conditions plus a daily forecast from Open-Meteo (free, no API key).
 * Config:
 *   latitude=43.5446
 *   longitude=-96.7311
 *   timezone=America/Chicago   (IANA name; decides where "today" starts)
 *   days=10                    (1-16)
 *   units=imperial             (or metric)
 *   refresh_minutes=30 */

#define FORECAST_URL "https://api.open-meteo.com/v1/forecast"
#define MAX_DAYS 16

typedef struct {
    char date[11]; /* YYYY-MM-DD */
    int code;      /* WMO weather code, -1 = unknown */
    double hi, lo;
    int precip;    /* max precipitation probability %, -1 = unknown */
} Day;

typedef struct {
    Page *page;
    char *url;
    char *latitude, *longitude, *timezone;
    int n_days;
    gboolean metric;
    int refresh_s;

    gboolean have_data;
    gboolean have_current;
    double temp, feels, wind;
    int code, humidity;
    Day days[MAX_DAYS];
    int got_days;

    time_t fetched_at;
    char *error;
    gboolean in_flight;
} WeatherState;

/* --- WMO weather codes --- */

typedef enum { WX_CLEAR, WX_PARTLY, WX_CLOUDY, WX_FOG, WX_DRIZZLE, WX_RAIN,
               WX_FREEZING, WX_SNOW, WX_STORM, WX_UNKNOWN } WxKind;

static WxKind wx_kind(int code)
{
    switch (code) {
    case 0: case 1: return WX_CLEAR;
    case 2: return WX_PARTLY;
    case 3: return WX_CLOUDY;
    case 45: case 48: return WX_FOG;
    case 51: case 53: case 55: return WX_DRIZZLE;
    case 61: case 63: case 65: case 80: case 81: case 82: return WX_RAIN;
    case 56: case 57: case 66: case 67: return WX_FREEZING;
    case 71: case 73: case 75: case 77: case 85: case 86: return WX_SNOW;
    case 95: case 96: case 99: return WX_STORM;
    default: return WX_UNKNOWN;
    }
}

static const char *wx_text(int code)
{
    switch (code) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Cloudy";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorms";
    case 96: case 99: return "Storms, hail";
    default: return "";
    }
}

/* --- Icons, drawn with cairo so they stay crisp on e-ink --- */

static void sun(cairo_t *cr, double cx, double cy, double r, double lw)
{
    cairo_set_line_width(cr, lw);
    cairo_new_sub_path(cr);
    cairo_arc(cr, cx, cy, r, 0, 2 * G_PI);
    cairo_stroke(cr);
    for (int i = 0; i < 8; i++) {
        double a = i * G_PI / 4;
        cairo_move_to(cr, cx + cos(a) * r * 1.35, cy + sin(a) * r * 1.35);
        cairo_line_to(cr, cx + cos(a) * r * 1.8, cy + sin(a) * r * 1.8);
    }
    cairo_stroke(cr);
}

static void cloud_path(cairo_t *cr, double cx, double cy, double s)
{
    cairo_new_path(cr);
    cairo_new_sub_path(cr);
    cairo_arc(cr, cx - 0.22 * s, cy + 0.05 * s, 0.2 * s, 0, 2 * G_PI);
    cairo_new_sub_path(cr);
    cairo_arc(cr, cx + 0.02 * s, cy - 0.08 * s, 0.27 * s, 0, 2 * G_PI);
    cairo_new_sub_path(cr);
    cairo_arc(cr, cx + 0.25 * s, cy + 0.07 * s, 0.18 * s, 0, 2 * G_PI);
    cairo_rectangle(cr, cx - 0.22 * s, cy, 0.47 * s, 0.25 * s);
}

/* Outline of the union of the cloud's shapes: stroke them all thick, then fill white. */
static void cloud(cairo_t *cr, double cx, double cy, double s, double lw)
{
    cloud_path(cr, cx, cy, s);
    cairo_set_line_width(cr, lw * 2);
    cairo_stroke(cr);
    cloud_path(cr, cx, cy, s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
}

static void flake(cairo_t *cr, double cx, double cy, double r, double lw)
{
    cairo_set_line_width(cr, lw * 0.8);
    for (int i = 0; i < 3; i++) {
        double a = i * G_PI / 3 + G_PI / 2;
        cairo_move_to(cr, cx - cos(a) * r, cy - sin(a) * r);
        cairo_line_to(cr, cx + cos(a) * r, cy + sin(a) * r);
    }
    cairo_stroke(cr);
}

static void draw_icon(cairo_t *cr, Rect r, int code)
{
    double s = MIN(r.w, r.h);
    double cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    double lw = MAX(1.5, s * 0.045);
    WxKind k = wx_kind(code);

    cairo_save(cr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    switch (k) {
    case WX_CLEAR:
        sun(cr, cx, cy, s * 0.2, lw);
        break;
    case WX_PARTLY:
        sun(cr, cx + s * 0.14, cy - s * 0.14, s * 0.14, lw);
        cloud(cr, cx - s * 0.04, cy + s * 0.1, s * 0.78, lw);
        break;
    case WX_CLOUDY:
    case WX_UNKNOWN:
        cloud(cr, cx, cy + s * 0.04, s * 0.95, lw);
        break;
    case WX_FOG:
        cairo_set_line_width(cr, lw);
        for (int i = 0; i < 4; i++) {
            double y = cy - s * 0.24 + i * s * 0.16;
            double inset = (i % 2) ? s * 0.1 : 0;
            cairo_move_to(cr, cx - s * 0.38 + inset, y);
            cairo_line_to(cr, cx + s * 0.38 - inset * 0.5, y);
        }
        cairo_stroke(cr);
        break;
    default: {
        /* Precipitation: cloud on top, symbols underneath. */
        double ccy = cy - s * 0.12;
        cloud(cr, cx, ccy, s * 0.85, lw);
        double y0 = cy + s * 0.22, y1 = cy + s * 0.42;
        double xs[3] = { cx - s * 0.2, cx, cx + s * 0.2 };
        cairo_set_line_width(cr, lw);
        for (int i = 0; i < 3; i++) {
            double x = xs[i];
            if (k == WX_RAIN || (k == WX_FREEZING && i != 1)) {
                cairo_move_to(cr, x + s * 0.04, y0);
                cairo_line_to(cr, x - s * 0.04, y1);
                cairo_stroke(cr);
            } else if (k == WX_DRIZZLE) {
                cairo_new_sub_path(cr);
                cairo_arc(cr, x, (y0 + y1) / 2, lw * 0.9, 0, 2 * G_PI);
                cairo_fill(cr);
            } else if (k == WX_SNOW || k == WX_FREEZING) {
                flake(cr, x, (y0 + y1) / 2, s * 0.08, lw);
            }
        }
        if (k == WX_STORM) {
            double b = s;
            cairo_move_to(cr, cx + 0.04 * b, cy + 0.04 * b);
            cairo_line_to(cr, cx - 0.1 * b, cy + 0.27 * b);
            cairo_line_to(cr, cx + 0.0 * b, cy + 0.27 * b);
            cairo_line_to(cr, cx - 0.06 * b, cy + 0.46 * b);
            cairo_line_to(cr, cx + 0.13 * b, cy + 0.2 * b);
            cairo_line_to(cr, cx + 0.03 * b, cy + 0.2 * b);
            cairo_close_path(cr);
            cairo_fill(cr);
        }
        break;
    }
    }
    cairo_restore(cr);
}

/* Small raindrop drawn before precipitation percentages. */
static void drop(cairo_t *cr, double cx, double cy, double h)
{
    double r = h * 0.3;
    cairo_new_sub_path(cr);
    cairo_move_to(cr, cx, cy - h / 2);
    cairo_curve_to(cr, cx + r * 0.4, cy - h * 0.15, cx + r, cy + h * 0.05, cx + r, cy + h / 2 - r);
    cairo_arc(cr, cx, cy + h / 2 - r, r, 0, G_PI);
    cairo_curve_to(cr, cx - r, cy + h * 0.05, cx - r * 0.4, cy - h * 0.15, cx, cy - h / 2);
    cairo_fill(cr);
}

static void draw_precip(cairo_t *cr, Rect r, int pct, UiAlign align)
{
    if (pct < 0)
        return;
    char buf[8];
    g_snprintf(buf, sizeof buf, "%d%%", pct);
    double dh = r.h * 0.6;
    double dw = dh * 0.7;
    Rect text = r;
    if (align == UI_CENTER) {
        /* Center drop + text together: estimate text as ~2.2 drop widths. */
        double total = dw * 1.4 + r.h * 1.4;
        double x0 = r.x + (r.w - total) / 2;
        drop(cr, x0 + dw / 2, r.y + r.h / 2, dh);
        text = ui_rect(x0 + dw * 1.4, r.y, r.w - (x0 - r.x) - dw * 1.4, r.h);
        ui_text_fit(cr, text, buf, FALSE, r.h * 0.75, UI_LEFT);
    } else {
        drop(cr, r.x + dw / 2, r.y + r.h / 2, dh);
        text = ui_rect(r.x + dw * 1.4, r.y, r.w - dw * 1.4, r.h);
        ui_text_fit(cr, text, buf, FALSE, r.h * 0.75, align);
    }
}

/* --- Data --- */

static void set_error(WeatherState *st, char *err)
{
    g_free(st->error);
    st->error = err;
    g_message("[%s] %s", st->page->section, err ? err : "ok");
}

static double num(cJSON *obj, const char *key, double def)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

static double num_at(cJSON *daily, const char *key, int i, double def)
{
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(daily, key);
    cJSON *v = cJSON_IsArray(arr) ? cJSON_GetArrayItem(arr, i) : NULL;
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

static void on_forecast(int status, cJSON *json, const char *err, gpointer user)
{
    WeatherState *st = user;
    st->in_flight = FALSE;

    cJSON *daily = json ? cJSON_GetObjectItemCaseSensitive(json, "daily") : NULL;
    cJSON *dates = daily ? cJSON_GetObjectItemCaseSensitive(daily, "time") : NULL;
    if (err || !cJSON_IsArray(dates)) {
        cJSON *reason = json ? cJSON_GetObjectItemCaseSensitive(json, "reason") : NULL;
        set_error(st, g_strdup(cJSON_IsString(reason) ? reason->valuestring
                               : err ? err : "unexpected forecast response"));
        app_request_redraw(st->page);
        return;
    }

    cJSON *cur = cJSON_GetObjectItemCaseSensitive(json, "current");
    st->have_current = cJSON_IsObject(cur);
    if (st->have_current) {
        st->temp = num(cur, "temperature_2m", NAN);
        st->feels = num(cur, "apparent_temperature", NAN);
        st->wind = num(cur, "wind_speed_10m", NAN);
        st->code = (int)num(cur, "weather_code", -1);
        st->humidity = (int)num(cur, "relative_humidity_2m", -1);
    }

    int n = MIN(cJSON_GetArraySize(dates), MAX_DAYS);
    for (int i = 0; i < n; i++) {
        Day *d = &st->days[i];
        cJSON *t = cJSON_GetArrayItem(dates, i);
        g_strlcpy(d->date, cJSON_IsString(t) ? t->valuestring : "", sizeof d->date);
        d->code = (int)num_at(daily, "weather_code", i, -1);
        d->hi = num_at(daily, "temperature_2m_max", i, NAN);
        d->lo = num_at(daily, "temperature_2m_min", i, NAN);
        d->precip = (int)num_at(daily, "precipitation_probability_max", i, -1);
    }
    st->got_days = n;
    st->have_data = TRUE;
    st->fetched_at = time(NULL);
    set_error(st, NULL);
    app_request_redraw(st->page);
}

static void fetch(WeatherState *st)
{
    if (st->in_flight)
        return;
    char *tz = g_uri_escape_string(st->timezone, NULL, FALSE);
    char *url = g_strdup_printf(
        "%s?latitude=%s&longitude=%s&timezone=%s&forecast_days=%d"
        "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m,relative_humidity_2m"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
        "&temperature_unit=%s&wind_speed_unit=%s",
        st->url, st->latitude, st->longitude, tz, st->n_days,
        st->metric ? "celsius" : "fahrenheit", st->metric ? "kmh" : "mph");
    const char *headers[] = { "Accept: application/json", "User-Agent: kindledash/0.1", NULL };
    HttpRequest req = { .url = url, .headers = headers };
    st->in_flight = TRUE;
    http_json_request(&req, on_forecast, st);
    g_free(url);
    g_free(tz);
}

static gboolean on_refresh_timer(gpointer data)
{
    fetch(data);
    return TRUE;
}

static gboolean on_first_fetch(gpointer data)
{
    WeatherState *st = data;
    fetch(st);
    g_timeout_add_seconds(st->refresh_s, on_refresh_timer, st);
    return FALSE;
}

static void weather_init(Page *p, GKeyFile *cfg, const char *section)
{
    WeatherState *st = g_new0(WeatherState, 1);
    st->page = p;
    st->url = cfg_string(cfg, section, "url", FORECAST_URL); /* override for testing */
    st->latitude = cfg_string(cfg, section, "latitude", "43.5446");
    st->longitude = cfg_string(cfg, section, "longitude", "-96.7311");
    st->timezone = cfg_string(cfg, section, "timezone", "America/Chicago");
    st->n_days = CLAMP(cfg_int(cfg, section, "days", 10), 1, MAX_DAYS);
    char *units = cfg_string(cfg, section, "units", "imperial");
    st->metric = g_ascii_strcasecmp(units, "metric") == 0;
    g_free(units);
    st->refresh_s = MAX(1, cfg_int(cfg, section, "refresh_minutes", 30)) * 60;
    p->state = st;
    g_timeout_add_seconds(1, on_first_fetch, st);
}

static void weather_on_show(Page *p)
{
    WeatherState *st = p->state;
    if (st->have_data && time(NULL) - st->fetched_at >= st->refresh_s)
        fetch(st);
}

/* --- Layout --- */

static void day_name(const Day *d, int index, char *name, size_t nlen, char *date, size_t dlen)
{
    struct tm tm = { 0 };
    if (sscanf(d->date, "%4d-%2d-%2d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) {
        g_strlcpy(name, "", nlen);
        g_strlcpy(date, "", dlen);
        return;
    }
    g_snprintf(date, dlen, "%d/%d", tm.tm_mon, tm.tm_mday);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_hour = 12;
    time_t t = timegm(&tm);
    gmtime_r(&t, &tm);
    static const char *const WD[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    g_strlcpy(name, index == 0 ? "Today" : WD[tm.tm_wday], nlen);
}

static void fmt_temp(char *buf, size_t len, double v)
{
    if (isnan(v))
        g_strlcpy(buf, "--", len);
    else
        g_snprintf(buf, len, "%.0f°", v);
}

static void draw_current(WeatherState *st, cairo_t *cr, Rect r)
{
    Rect icon = ui_rect(r.x, r.y, r.h, r.h);
    Rect rest = ui_rect(r.x + r.h * 1.1, r.y, r.w - r.h * 1.1, r.h);
    Rect temp = ui_rect(rest.x, rest.y, rest.w * 0.38, rest.h);
    Rect info = ui_rect(rest.x + rest.w * 0.4, rest.y, rest.w * 0.6, rest.h);

    int code = st->have_current ? st->code : st->days[0].code;
    draw_icon(cr, ui_inset(icon, r.h * 0.06), code);

    char t[16];
    fmt_temp(t, sizeof t, st->have_current ? st->temp : st->days[0].hi);
    ui_text_fit(cr, ui_inset(temp, r.h * 0.08), t, TRUE, r.h * 0.75, UI_LEFT);

    char hl[48], feels[48], wind[64];
    char hi[16], lo[16], fl[16];
    fmt_temp(hi, sizeof hi, st->days[0].hi);
    fmt_temp(lo, sizeof lo, st->days[0].lo);
    g_snprintf(hl, sizeof hl, "High %s  ·  Low %s", hi, lo);
    fmt_temp(fl, sizeof fl, st->feels);
    g_snprintf(feels, sizeof feels, "Feels like %s", fl);
    if (st->humidity >= 0)
        g_snprintf(wind, sizeof wind, "Wind %.0f %s  ·  Humidity %d%%",
                   st->wind, st->metric ? "km/h" : "mph", st->humidity);
    else
        g_snprintf(wind, sizeof wind, "Wind %.0f %s", st->wind, st->metric ? "km/h" : "mph");

    Rect lines[4];
    ui_split(ui_inset(info, r.h * 0.04), 4, TRUE, 0, lines);
    double px = lines[0].h * 0.75;
    ui_text_fit(cr, lines[0], wx_text(code), TRUE, px * 1.15, UI_LEFT);
    ui_text_fit(cr, lines[1], hl, FALSE, px, UI_LEFT);
    if (st->have_current) {
        ui_text_fit(cr, lines[2], feels, FALSE, px, UI_LEFT);
        ui_text_fit(cr, lines[3], wind, FALSE, px, UI_LEFT);
    }
}

/* Landscape: one column per day. */
static void draw_columns(WeatherState *st, cairo_t *cr, Rect r, double line)
{
    int n = st->got_days;
    Rect cols[n];
    ui_split(r, n, FALSE, 0, cols);
    for (int i = 0; i < n; i++) {
        const Day *d = &st->days[i];
        Rect c = ui_inset(cols[i], cols[i].w * 0.06);
        Rect name = ui_take_top(&c, c.h * 0.13);
        Rect date = ui_take_top(&c, c.h * 0.1);
        Rect icon = ui_take_top(&c, c.h * 0.4);
        Rect hi = ui_take_top(&c, c.h * 0.36);
        Rect lo = ui_take_top(&c, c.h * 0.55);
        Rect pp = c;

        char dn[16], ds[16], hs[16], ls[16];
        day_name(d, i, dn, sizeof dn, ds, sizeof ds);
        fmt_temp(hs, sizeof hs, d->hi);
        fmt_temp(ls, sizeof ls, d->lo);
        ui_text_fit(cr, name, dn, TRUE, name.h * 0.85, UI_CENTER);
        ui_text_fit(cr, date, ds, FALSE, date.h * 0.8, UI_CENTER);
        draw_icon(cr, ui_inset(icon, icon.h * 0.05), d->code);
        ui_text_fit(cr, hi, hs, TRUE, hi.h * 0.85, UI_CENTER);
        ui_text_fit(cr, lo, ls, FALSE, lo.h * 0.8, UI_CENTER);
        draw_precip(cr, ui_inset(pp, pp.h * 0.1), d->precip, UI_CENTER);

        if (i > 0)
            ui_vline(cr, cols[i].x, r.y + r.h * 0.04, r.y + r.h * 0.96, line);
    }
}

/* Portrait: one row per day. */
static void draw_rows(WeatherState *st, cairo_t *cr, Rect r, double line)
{
    int n = st->got_days;
    Rect rows[n];
    ui_split(r, n, TRUE, 0, rows);
    for (int i = 0; i < n; i++) {
        const Day *d = &st->days[i];
        Rect row = ui_inset(rows[i], rows[i].h * 0.1);
        double x = row.x, w = row.w;
        Rect name = ui_rect(x, row.y, w * 0.17, row.h);
        Rect icon = ui_rect(x + w * 0.18, row.y, w * 0.1, row.h);
        Rect text = ui_rect(x + w * 0.31, row.y, w * 0.33, row.h);
        Rect temps = ui_rect(x + w * 0.65, row.y, w * 0.2, row.h);
        Rect pp = ui_rect(x + w * 0.87, row.y, w * 0.13, row.h);

        char dn[16], ds[16], hs[16], ls[16], hl[40];
        day_name(d, i, dn, sizeof dn, ds, sizeof ds);
        fmt_temp(hs, sizeof hs, d->hi);
        fmt_temp(ls, sizeof ls, d->lo);
        g_snprintf(hl, sizeof hl, "%s / %s", hs, ls);
        ui_text_fit(cr, name, dn, TRUE, row.h * 0.7, UI_LEFT);
        draw_icon(cr, icon, d->code);
        ui_text_fit(cr, text, wx_text(d->code), FALSE, row.h * 0.55, UI_LEFT);
        ui_text_fit(cr, temps, hl, TRUE, row.h * 0.6, UI_RIGHT);
        draw_precip(cr, ui_inset(pp, row.h * 0.15), d->precip, UI_RIGHT);

        if (i > 0)
            ui_hline(cr, r.x, r.x + r.w, rows[i].y, line);
    }
}

static void weather_draw(Page *p, cairo_t *cr, int w, int h)
{
    WeatherState *st = p->state;
    double u = ui_unit(w, h);
    double line = MAX(1, u * 0.25);
    Rect area = ui_inset(ui_rect(0, 0, w, h), u);

    if (!st->have_data || st->got_days == 0) {
        const char *msg = st->error ? st->error : "Loading forecast…";
        ui_text_wrap(cr, ui_inset(area, area.w * 0.05), msg, FALSE, h * 0.04, UI_CENTER);
        return;
    }

    gboolean landscape = ui_is_landscape(w, h);
    Rect status = ui_take_bottom(&area, area.h * 0.05);
    Rect cur = ui_take_top(&area, area.h * (landscape ? 0.28 : 0.17));
    ui_take_top(&area, u * 1.5);
    ui_hline(cr, area.x, area.x + area.w, area.y, MAX(2, u * 0.5));
    ui_take_top(&area, u * 1.5);

    draw_current(st, cr, cur);
    if (landscape)
        draw_columns(st, cr, area, line);
    else
        draw_rows(st, cr, area, line);

    char when[16], buf[256];
    struct tm tm;
    localtime_r(&st->fetched_at, &tm);
    strftime(when, sizeof when, "%H:%M", &tm);
    if (st->error)
        g_snprintf(buf, sizeof buf, "STALE (updated %s) · %s", when, st->error);
    else
        g_snprintf(buf, sizeof buf, "Updated %s · Open-Meteo", when);
    ui_text_wrap(cr, status, buf, st->error != NULL, MIN(status.h * 0.7, u * 2.4), UI_CENTER);
}

const PageType weather_type = {
    .name = "weather",
    .init = weather_init,
    .draw = weather_draw,
    .on_show = weather_on_show,
    .redraw_interval_s = 0,
};
