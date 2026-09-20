/**
 * @file webui_transport_linux.cpp
 *
 * The simulator's HTTP server.
 *
 * Hand-written because esp_http_server cannot be used here: its linux port
 * creates its threads with a raw pthread_create(), which inherits an unblocked
 * signal mask, so the process-directed SIGALRM that drives the FreeRTOS tick
 * is delivered to a thread with no task control block and vTaskSwitchContext()
 * is called from outside FreeRTOS. It asserts within a request or two.
 *
 * One rule governs everything below, and it is the reason for every select()
 * in it: on the FreeRTOS POSIX simulator, *only* select() is wrapped
 * (freertos/esp_additions/FreeRTOSSimulator_wrappers.c, built when
 * CONFIG_LWIP_ENABLE is off). The wrapper resolves the real select() through
 * dlsym and polls with vTaskDelay so that the SIGALRM-driven EINTR does not
 * escape. Every other blocking call -- accept(), recv() -- parks the whole
 * cooperative scheduler: measured, and the symptom is that no SDL window ever
 * appears while curl is served perfectly. So nothing here blocks on anything
 * but select().
 *
 * It speaks enough HTTP/1.1 for a browser and for curl: one request per
 * connection, Connection: close, chunked responses. No keep-alive, no
 * pipelining, no TLS.
 */

#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_LINUX

#include "webui_transport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "webui";

/* Request line plus headers. A browser sends perhaps 600 bytes; anything much
 * larger is not a request this server is meant to serve. */
#define REQUEST_HEAD_MAX 2048

/* Same reasoning as the device's: the settings form is about 1.5 KB. */
#define REQUEST_BODY_MAX 4096

#define SERVER_TASK_STACK 8192

struct linux_request_s
{
    int    fd;
    char  *body;          /* the form body, when it was buffered here */
    size_t body_total;    /* Content-Length */
    size_t body_consumed; /* how much a streaming handler has taken */
    char  *body_pending;  /* body bytes that arrived with the head */
    size_t body_pending_len;
    bool   chunked;
};

static uint16_t server_port;

/* -------------------------------------------------------------- socket I/O */

/* Wait for `fd` to become readable. The only blocking primitive in this file,
 * for the reason in the header comment. */
static bool wait_readable(int fd, int timeout_ms)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int n = select(fd + 1, &set, NULL, NULL, &tv);

    return (n > 0 && FD_ISSET(fd, &set));
}

static void write_all(int fd, const char *data, size_t len)
{
    while (len > 0)
    {
        ssize_t n = send(fd, data, len, MSG_NOSIGNAL);

        if (n <= 0)
        {
            if (n < 0 && errno == EINTR)
                continue; /* the tick, not a failure */

            return;
        }

        data += n;
        len -= (size_t)n;
    }
}

static void write_str(int fd, const char *s)
{
    write_all(fd, s, strlen(s));
}

/* ------------------------------------------------------------ the responses */

static void send_head(int fd, int status, const char *content_type, const char *extra,
                      long content_length)
{
    const char *reason = "OK";

    switch (status)
    {
    case 204: reason = "No Content"; break;
    case 302: reason = "Found"; break;
    case 303: reason = "See Other"; break;
    case 400: reason = "Bad Request"; break;
    case 404: reason = "Not Found"; break;
    case 500: reason = "Internal Server Error"; break;
    default: break;
    }

    char head[512];
    int  len = snprintf(head, sizeof(head),
                        "HTTP/1.1 %d %s\r\n"
                        "Content-Type: %s\r\n"
                        "Connection: close\r\n"
                        "%s",
                        status, reason, content_type, (extra != NULL) ? extra : "");

    if (content_length >= 0)
        len += snprintf(head + len, sizeof(head) - len, "Content-Length: %ld\r\n", content_length);
    else
        len += snprintf(head + len, sizeof(head) - len, "Transfer-Encoding: chunked\r\n");

    snprintf(head + len, sizeof(head) - len, "\r\n");

    write_str(fd, head);
}

void webui_begin_chunked(webui_request_t *req, const char *content_type)
{
    struct linux_request_s *impl = (struct linux_request_s *)req->impl;

    send_head(impl->fd, 200, content_type, NULL, -1);
    impl->chunked = true;
}

void webui_write(webui_request_t *req, const char *data, size_t len)
{
    struct linux_request_s *impl = (struct linux_request_s *)req->impl;

    if (len == 0)
        return; /* a zero-length chunk would end the response */

    char header[24];

    snprintf(header, sizeof(header), "%zx\r\n", len);
    write_str(impl->fd, header);
    write_all(impl->fd, data, len);
    write_str(impl->fd, "\r\n");
}

void webui_end_chunked(webui_request_t *req)
{
    struct linux_request_s *impl = (struct linux_request_s *)req->impl;

    write_str(impl->fd, "0\r\n\r\n");
    impl->chunked = false;
}

void webui_send(webui_request_t *req, int status, const char *content_type, const char *body)
{
    struct linux_request_s *impl = (struct linux_request_s *)req->impl;
    size_t                  len = (body != NULL) ? strlen(body) : 0;

    send_head(impl->fd, status, content_type, NULL, (long)len);

    if (len > 0)
        write_all(impl->fd, body, len);
}

void webui_redirect(webui_request_t *req, int status, const char *location)
{
    struct linux_request_s *impl = (struct linux_request_s *)req->impl;
    char                    extra[256];

    snprintf(extra, sizeof(extra), "Location: %s\r\n", location);
    send_head(impl->fd, status, "text/plain", extra, 0);
}

size_t webui_body_length(webui_request_t *req)
{
    struct linux_request_s *impl = (struct linux_request_s *)req->impl;

    return impl->body_total;
}

int webui_body_read(webui_request_t *req, char *buf, size_t buf_size)
{
    struct linux_request_s *impl = (struct linux_request_s *)req->impl;

    if (impl->body_consumed >= impl->body_total)
        return 0;

    size_t want = impl->body_total - impl->body_consumed;

    if (want > buf_size)
        want = buf_size;

    /* Whatever arrived in the same read as the headers comes out first. */
    if (impl->body_pending_len > 0)
    {
        size_t n = (impl->body_pending_len < want) ? impl->body_pending_len : want;

        memcpy(buf, impl->body_pending, n);
        impl->body_pending += n;
        impl->body_pending_len -= n;
        impl->body_consumed += n;

        return (int)n;
    }

    if (wait_readable(impl->fd, 20000) == false)
        return -1;

    ssize_t n = recv(impl->fd, buf, want, 0);

    if (n <= 0)
        return (n == 0) ? 0 : -1;

    impl->body_consumed += (size_t)n;

    return (int)n;
}

/* -------------------------------------------------------------- the request */

static void serve(int fd)
{
    char head[REQUEST_HEAD_MAX + 1];
    size_t head_len = 0;
    char *blank = NULL;

    /* Read until the blank line that ends the headers. */
    while (head_len < REQUEST_HEAD_MAX)
    {
        if (wait_readable(fd, 5000) == false)
            return;

        ssize_t n = recv(fd, head + head_len, REQUEST_HEAD_MAX - head_len, 0);

        if (n <= 0)
        {
            if (n < 0 && errno == EINTR)
                continue;

            return;
        }

        head_len += (size_t)n;
        head[head_len] = '\0';

        blank = strstr(head, "\r\n\r\n");

        if (blank != NULL)
            break;
    }

    if (blank == NULL)
        return;

    /* "METHOD /path?query HTTP/1.1" */
    char *method_end = strchr(head, ' ');

    if (method_end == NULL)
        return;

    *method_end = '\0';

    enum webui_method_e method;

    if (strcmp(head, "GET") == 0)
        method = WEBUI_GET;
    else if (strcmp(head, "POST") == 0)
        method = WEBUI_POST;
    else
    {
        send_head(fd, 400, "text/plain", NULL, 0);
        return;
    }

    char *target = method_end + 1;
    char *target_end = strchr(target, ' ');

    if (target_end == NULL)
        return;

    *target_end = '\0';

    char *query = strchr(target, '?');

    if (query != NULL)
        *query++ = '\0';

    /* Content-Length, case-insensitively, out of the header block. */
    size_t content_length = 0;

    for (char *line = target_end + 1; line != NULL && line < blank;)
    {
        char *eol = strstr(line, "\r\n");

        if (eol == NULL || eol >= blank)
            break;

        if (strncasecmp(line, "content-length:", 15) == 0)
            content_length = (size_t)strtoul(line + 15, NULL, 10);

        line = eol + 2;
    }

    struct linux_request_s impl = {};
    webui_request_t        req = {};

    impl.fd = fd;
    impl.body_total = content_length;
    impl.body_pending = blank + 4;
    impl.body_pending_len = head_len - (size_t)(impl.body_pending - head);
    req.impl = &impl;

    if (method == WEBUI_GET)
    {
        req.args = query;
        req.args_len = (query != NULL) ? strlen(query) : 0;
    }
    else if (webui_transport_route_is_stream(target, method) == false)
    {
        if (content_length > REQUEST_BODY_MAX)
        {
            send_head(fd, 400, "text/plain", NULL, 0);
            return;
        }

        impl.body = (char *)calloc(1, content_length + 1);

        if (impl.body == NULL)
        {
            send_head(fd, 500, "text/plain", NULL, 0);
            return;
        }

        size_t got = 0;

        while (got < content_length)
        {
            int n = webui_body_read(&req, impl.body + got, content_length - got);

            if (n <= 0)
                break;

            got += (size_t)n;
        }

        req.args = impl.body;
        req.args_len = got;
    }

    webui_transport_dispatch(&req, target, method);

    free(impl.body);
}

static void server_task(void *arg)
{
    (void)arg;

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_fd < 0)
    {
        ESP_LOGE(TAG, "socket: %s", strerror(errno));
        vTaskDelete(NULL);
        return;
    }

    int one = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(server_port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        ESP_LOGE(TAG, "bind to port %u: %s", (unsigned)server_port, strerror(errno));
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_fd, 4) != 0)
    {
        ESP_LOGE(TAG, "listen: %s", strerror(errno));
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "listening on port %u", (unsigned)server_port);

    for (;;)
    {
        /* Never a bare accept(): it would park the scheduler, and the UI with
         * it. One second so the task is still responsive to nothing at all. */
        if (wait_readable(listen_fd, 1000) == false)
            continue;

        int fd = accept(listen_fd, NULL, NULL);

        if (fd < 0)
            continue;

        serve(fd);
        close(fd);
    }
}

bool webui_transport_start(uint16_t port)
{
    /* The device serves on 80, which an unprivileged process cannot bind. Rather
     * than failing to start -- and leaving the web interface untestable on a
     * desktop, which is half of why this transport exists -- the simulator moves
     * to 8780 and says so. OHEZ_WEBUI_PORT overrides either. */
    const char *env = getenv("OHEZ_WEBUI_PORT");

    if (env != NULL && env[0] != '\0')
        server_port = (uint16_t)atoi(env);
    else if (port < 1024)
        server_port = 8780;
    else
        server_port = port;

    /* A task of its own, as on the device, so that a browser waiting on a
     * response cannot hold up the screen. */
    return (xTaskCreate(server_task, "webui", SERVER_TASK_STACK, NULL, 4, NULL) == pdPASS);
}

uint16_t webui_transport_port(void)
{
    return server_port;
}

void webui_transport_loop(void)
{
    /* The task above does the work. */
}

#endif /* CONFIG_IDF_TARGET_LINUX */
