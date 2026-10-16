#define _GNU_SOURCE
#include "http_json.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static char *ca_bundle;

typedef struct {
    HttpJsonCb cb;
    gpointer user;
    GString *out;
    gboolean eof;
    gboolean exited;
    int exit_status;
} Job;

void http_json_set_ca_bundle(const char *path)
{
    g_free(ca_bundle);
    ca_bundle = (path && *path && g_file_test(path, G_FILE_TEST_IS_REGULAR)) ? g_strdup(path) : NULL;
    if (path && *path && !ca_bundle)
        g_warning("CA bundle %s not found, using curl's default certificate store", path);
}

static const char *curl_error_text(int code)
{
    switch (code) {
    case 6: return "DNS lookup failed (Wi-Fi off?)";
    case 7: return "could not connect";
    case 28: return "request timed out";
    case 35: return "TLS handshake failed";
    case 51: case 60: case 77: return "TLS certificate problem (check ca_bundle)";
    default: return NULL;
    }
}

static void job_finish(Job *job)
{
    int http_status = 0;
    char *err = NULL;
    cJSON *json = NULL;

    /* curl -w appends "\n<code>" after the body. */
    char *nl = strrchr(job->out->str, '\n');
    if (nl) {
        http_status = atoi(nl + 1);
        *nl = '\0';
    }

    if (!WIFEXITED(job->exit_status) || WEXITSTATUS(job->exit_status) != 0) {
        int code = WIFEXITED(job->exit_status) ? WEXITSTATUS(job->exit_status) : -1;
        const char *txt = curl_error_text(code);
        err = txt ? g_strdup_printf("%s (curl %d)", txt, code) : g_strdup_printf("curl failed (%d)", code);
        http_status = 0;
    } else {
        json = cJSON_Parse(job->out->str);
        if (http_status < 200 || http_status >= 300)
            err = g_strdup_printf("HTTP %d", http_status);
        else if (!json)
            err = g_strdup("invalid JSON in response");
    }

    job->cb(http_status, json, err, job->user);

    cJSON_Delete(json);
    g_free(err);
    g_string_free(job->out, TRUE);
    g_free(job);
}

static gboolean on_stdout(GIOChannel *ch, GIOCondition cond, gpointer data)
{
    Job *job = data;
    char buf[4096];
    gsize n;
    GIOStatus st;

    do {
        st = g_io_channel_read_chars(ch, buf, sizeof buf, &n, NULL);
        if (n > 0)
            g_string_append_len(job->out, buf, n);
    } while (st == G_IO_STATUS_NORMAL && n > 0);

    if (st == G_IO_STATUS_AGAIN && !(cond & (G_IO_HUP | G_IO_ERR)))
        return TRUE;

    g_io_channel_shutdown(ch, FALSE, NULL);
    g_io_channel_unref(ch);
    job->eof = TRUE;
    if (job->exited)
        job_finish(job);
    return FALSE;
}

static void on_child_exit(GPid pid, gint status, gpointer data)
{
    Job *job = data;
    g_spawn_close_pid(pid);
    job->exit_status = status;
    job->exited = TRUE;
    if (job->eof)
        job_finish(job);
}

/* Quote a value for a curl config file line. */
static void append_quoted(GString *s, const char *key, const char *val)
{
    g_string_append_printf(s, "%s = \"", key);
    for (const char *p = val; *p; p++) {
        if (*p == '"' || *p == '\\')
            g_string_append_c(s, '\\');
        if (*p == '\n')
            g_string_append(s, "\\n");
        else
            g_string_append_c(s, *p);
    }
    g_string_append(s, "\"\n");
}

static void write_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t w = write(fd, buf, len);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            g_warning("writing to curl stdin: %s", g_strerror(errno));
            return;
        }
        buf += w;
        len -= w;
    }
}

gboolean http_json_request(const HttpRequest *req, HttpJsonCb cb, gpointer user)
{
    char timeout[16];
    g_snprintf(timeout, sizeof timeout, "%d", req->timeout_s > 0 ? req->timeout_s : 20);

    const char *argv[16];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-sS";
    argv[i++] = "-m";
    argv[i++] = timeout;
    argv[i++] = "-w";
    argv[i++] = "\n%{http_code}";
    if (ca_bundle) {
        argv[i++] = "--cacert";
        argv[i++] = ca_bundle;
    }
    argv[i++] = "-K";
    argv[i++] = "-";
    argv[i] = NULL;

    GString *conf = g_string_new(NULL);
    append_quoted(conf, "url", req->url);
    if (req->method)
        append_quoted(conf, "request", req->method);
    for (const char *const *h = req->headers; h && *h; h++)
        append_quoted(conf, "header", *h);
    if (req->body)
        append_quoted(conf, "data-raw", req->body);

    GPid pid;
    int in_fd, out_fd;
    GError *gerr = NULL;
    if (!g_spawn_async_with_pipes(NULL, (char **)argv, NULL,
                                  G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                  NULL, NULL, &pid, &in_fd, &out_fd, NULL, &gerr)) {
        char *msg = g_strdup_printf("cannot run curl: %s", gerr->message);
        g_error_free(gerr);
        g_string_free(conf, TRUE);
        cb(0, NULL, msg, user);
        g_free(msg);
        return FALSE;
    }

    write_all(in_fd, conf->str, conf->len);
    close(in_fd);
    g_string_free(conf, TRUE);

    Job *job = g_new0(Job, 1);
    job->cb = cb;
    job->user = user;
    job->out = g_string_new(NULL);

    GIOChannel *ch = g_io_channel_unix_new(out_fd);
    g_io_channel_set_close_on_unref(ch, TRUE);
    g_io_channel_set_encoding(ch, NULL, NULL);
    g_io_channel_set_flags(ch, G_IO_FLAG_NONBLOCK, NULL);
    g_io_add_watch(ch, G_IO_IN | G_IO_HUP | G_IO_ERR, on_stdout, job);
    g_child_watch_add(pid, on_child_exit, job);
    return TRUE;
}
