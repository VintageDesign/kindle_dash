#ifndef KD_HTTP_JSON_H
#define KD_HTTP_JSON_H

#include <glib.h>
#include "../third_party/cJSON.h"

/* Called on the main loop when a request finishes.
 * http_status is 0 if no HTTP response was received (then err is set).
 * json is NULL if the body was not valid JSON; it is freed after the callback returns. */
typedef void (*HttpJsonCb)(int http_status, cJSON *json, const char *err, gpointer user);

typedef struct {
    const char *url;
    const char *method;          /* NULL = GET */
    const char *const *headers;  /* NULL-terminated, "Name: value" */
    const char *body;            /* optional request body */
    int timeout_s;               /* 0 = 20s */
} HttpRequest;

/* Path to a CA bundle for curl; NULL or missing file = curl's default store. */
void http_json_set_ca_bundle(const char *path);

/* Run the request with the system curl binary, asynchronously.
 * Headers and the URL are passed on curl's stdin so secrets in them don't show up in ps. */
gboolean http_json_request(const HttpRequest *req, HttpJsonCb cb, gpointer user);

#endif
