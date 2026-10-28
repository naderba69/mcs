/* Run with: cc -std=gnu99 -pthread -Isrc tests/log-viewer-regression.c src/log-viewer.c -o /tmp/log-viewer-regression && /tmp/log-viewer-regression */
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "log-viewer.h"

struct http_request { const char *search; const char *level; };
struct tcp_buffer_data;
static char response[131072];
static size_t used;
void debugf(uint32_t flag, char *fmt, ...) { (void)flag; (void)fmt; }
char *isset_get(struct http_request *req, char *name)
{
    if (!strcmp(name, "search")) return (char *)req->search;
    if (!strcmp(name, "level")) return (char *)req->level;
    return NULL;
}
void tcp_init(struct tcp_buffer_data *buf) { (void)buf; used = 0; response[0] = 0; }
void tcp_flush(struct tcp_buffer_data *buf, int sock) { (void)buf; (void)sock; }
void tcp_writestr(struct tcp_buffer_data *buf, int sock, const char *text)
{
    size_t n = strlen(text); (void)buf; (void)sock;
    assert(used + n < sizeof(response));
    memcpy(response + used, text, n + 1); used += n;
}
int main(void)
{
    struct http_request req = {"1884%3A000000%3A1136", "3"};
    log_viewer_init();
    log_viewer_add(LOG_LEVEL_INFO, "test", "ECM 1884:000000:1136 received");
    http_send_log_viewer(1, &req);
    assert(strstr(response, "ECM 1884:000000:1136 received"));
    assert(strstr(response, "</html>"));
    req.search = "%3Cscript%3E";
    http_send_log_viewer(1, &req);
    assert(strstr(response, "&lt;script&gt;"));
    assert(!strstr(response, "<script></b>"));
    req.search = "no-match";
    http_send_log_viewer(1, &req);
    assert(strstr(response, "آخر 0 سجل"));
    puts("log viewer regression passed");
    return 0;
}
