/* Tiny HTTP server + JSON API for the theatre, written in plain C.
   Works on Linux/macOS (POSIX sockets) and Windows (Winsock). */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET sock_t;
  #define CLOSESOCK closesocket
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <signal.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  typedef int sock_t;
  #define INVALID_SOCKET (-1)
  #define CLOSESOCK close
#endif

#include "theatre.h"

#define REQ_MAX 65536

static Theatre theatre;

/* ---------- growable string buffer ---------- */

typedef struct { char *buf; size_t len, cap; } SB;

static void sb_init(SB *s) {
    s->cap = 4096;
    s->len = 0;
    s->buf = malloc(s->cap);
    if (!s->buf) { perror("malloc"); exit(1); }
    s->buf[0] = '\0';
}

static void sb_putf(SB *s, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) { va_end(ap2); return; }
    if (s->len + (size_t)need + 1 > s->cap) {
        while (s->len + (size_t)need + 1 > s->cap) s->cap *= 2;
        s->buf = realloc(s->buf, s->cap);
        if (!s->buf) { perror("realloc"); exit(1); }
    }
    vsnprintf(s->buf + s->len, (size_t)need + 1, fmt, ap2);
    va_end(ap2);
    s->len += (size_t)need;
}

/* writes "text" with JSON escaping */
static void sb_json_str(SB *s, const char *str) {
    sb_putf(s, "\"");
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        if (*p == '"') sb_putf(s, "\\\"");
        else if (*p == '\\') sb_putf(s, "\\\\");
        else if (*p < 0x20) sb_putf(s, "\\u%04x", *p);
        else sb_putf(s, "%c", *p);
    }
    sb_putf(s, "\"");
}

/* ---------- sending responses ---------- */

static void send_all(sock_t c, const char *data, size_t len) {
    while (len > 0) {
        int n = (int)send(c, data, (int)len, 0);
        if (n <= 0) return;
        data += n;
        len -= (size_t)n;
    }
}

static void send_response(sock_t c, int status, const char *ctype, const char *body, size_t len) {
    const char *text = status == 200 ? "OK" : status == 400 ? "Bad Request" : "Not Found";
    char head[256];
    int h = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        status, text, ctype, len);
    send_all(c, head, (size_t)h);
    send_all(c, body, len);
}

static void send_json(sock_t c, int status, SB *s) {
    send_response(c, status, "application/json; charset=utf-8", s->buf, s->len);
    free(s->buf);
}

static void send_error(sock_t c, int status, const char *msg) {
    SB s;
    sb_init(&s);
    sb_putf(&s, "{\"error\":");
    sb_json_str(&s, msg);
    sb_putf(&s, "}");
    send_json(c, status, &s);
}

/* ---------- tiny JSON reader (enough for our request bodies) ---------- */

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

/* returns a pointer to the value of "key", or NULL */
static const char *json_find(const char *body, const char *key) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(body, pat);
    if (!p) return NULL;
    p = skip_ws(p + strlen(pat));
    if (*p != ':') return NULL;
    return skip_ws(p + 1);
}

/* reads a JSON string starting at p (on the opening quote) into out */
static const char *read_json_string(const char *p, char *out, size_t outsz) {
    size_t n = 0;
    p++; /* opening quote */
    while (*p && *p != '"') {
        char ch = *p;
        if (ch == '\\' && p[1]) {
            p++;
            if (*p == 'n') ch = '\n';
            else if (*p == 't') ch = '\t';
            else if (*p == 'u') { ch = '?'; for (int i = 0; i < 4 && p[1]; i++) p++; }
            else ch = *p;
        }
        if (n + 1 < outsz) out[n++] = ch;
        p++;
    }
    out[n] = '\0';
    return *p == '"' ? p + 1 : p;
}

static int json_get_string(const char *body, const char *key, char *out, size_t outsz) {
    const char *p = json_find(body, key);
    if (!p || *p != '"') return 0;
    read_json_string(p, out, outsz);
    return 1;
}

static int json_get_int(const char *body, const char *key, int *out) {
    const char *p = json_find(body, key);
    if (!p) return 0;
    char *end;
    double v = strtod(p, &end);
    if (end == p || v != (double)(long)v || v < -1e9 || v > 1e9) return 0;
    *out = (int)v;
    return 1;
}

/* fills labels[] from "seats": ["A1","A2"]; returns count, or -1 if too many */
static int json_get_seats(const char *body, char labels[][LABEL_LEN], int max) {
    const char *p = json_find(body, "seats");
    int n = 0;
    if (!p || *p != '[') return 0;
    p = skip_ws(p + 1);
    while (*p == '"') {
        if (n >= max) return -1;
        p = read_json_string(p, labels[n], LABEL_LEN);
        n++;
        p = skip_ws(p);
        if (*p == ',') p = skip_ws(p + 1);
    }
    return n;
}

/* trims, limits to 40 bytes (without cutting a UTF-8 character) */
static int clean_name(const char *body, char *out) {
    char raw[256];
    if (!json_get_string(body, "name", raw, sizeof raw)) return 0;
    char *start = raw;
    while (*start && isspace((unsigned char)*start)) start++;
    size_t len = strlen(start);
    while (len > 0 && isspace((unsigned char)start[len - 1])) len--;
    if (len == 0) return 0;
    if (len > NAME_LEN - 1) {
        len = NAME_LEN - 1;
        while (len > 0 && ((unsigned char)start[len] & 0xC0) == 0x80) len--;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return 1;
}

/* ---------- building the theatre JSON ---------- */

static void write_booking_seats(SB *s, int id) {
    int first = 1;
    sb_putf(s, "[");
    for (int r = 0; r < ROWS; r++)
        for (SeatNode *cur = theatre.rows[r].head; cur; cur = cur->next)
            if (cur->bookingId == id) {
                sb_putf(s, "%s\"%c%d\"", first ? "" : ",", cur->row, cur->seatNo);
                first = 0;
            }
    sb_putf(s, "]");
}

static void write_theatre(SB *s, int lastId, int split) {
    sb_putf(s, "{\"rows\":[");
    for (int r = 0; r < ROWS; r++) {
        SeatRow *row = &theatre.rows[r];
        sb_putf(s, "%s{\"label\":\"%c\",\"seats\":[", r ? "," : "", row->label);
        for (SeatNode *cur = row->head; cur; cur = cur->next) {
            sb_putf(s, "%s{\"seatNo\":%d,\"label\":\"%c%d\",\"bookingId\":", cur == row->head ? "" : ",",
                    cur->seatNo, cur->row, cur->seatNo);
            if (cur->bookingId) sb_putf(s, "%d}", cur->bookingId);
            else sb_putf(s, "null}");
        }
        sb_putf(s, "]}");
    }
    sb_putf(s, "],\"bookings\":[");
    for (Booking *b = theatre.bookings; b; b = b->next) {
        sb_putf(s, "%s{\"id\":%d,\"name\":", b == theatre.bookings ? "" : ",", b->id);
        sb_json_str(s, b->name);
        sb_putf(s, ",\"seats\":");
        write_booking_seats(s, b->id);
        sb_putf(s, "}");
    }
    int total = MAX_SEATS, free_seats = theatre_free_count(&theatre);
    sb_putf(s, "],\"stats\":{\"total\":%d,\"free\":%d,\"booked\":%d}", total, free_seats, total - free_seats);
    if (lastId) {
        Booking *b = theatre_find_booking(&theatre, lastId);
        sb_putf(s, ",\"last\":{\"id\":%d,\"name\":", lastId);
        sb_json_str(s, b ? b->name : "");
        sb_putf(s, ",\"seats\":");
        write_booking_seats(s, lastId);
        sb_putf(s, ",\"split\":%s}", split ? "true" : "false");
    }
    sb_putf(s, "}");
}

static void send_theatre(sock_t c, int lastId, int split) {
    SB s;
    sb_init(&s);
    write_theatre(&s, lastId, split);
    send_json(c, 200, &s);
}

/* ---------- API routes ---------- */

static void handle_api(sock_t c, const char *method, const char *path, const char *body) {
    char err[128], name[NAME_LEN];

    if (!strcmp(method, "GET") && !strcmp(path, "/api/theatre")) {
        send_theatre(c, 0, 0);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/book/seats")) {
        static char labels[MAX_SEATS][LABEL_LEN];
        int n = json_get_seats(body, labels, MAX_SEATS);
        if (n == 0) return send_error(c, 400, "Select at least one seat");
        if (n < 0) return send_error(c, 400, "Too many seats selected");
        if (!clean_name(body, name)) return send_error(c, 400, "Enter a name for the booking");
        if (theatre_book_seats(&theatre, name, labels, n, err, sizeof err) < 0)
            return send_error(c, 400, err);
        send_theatre(c, 0, 0);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/book/auto")) {
        int count, split;
        if (!json_get_int(body, "count", &count) || count < 1 || count > 10)
            return send_error(c, 400, "Group size must be between 1 and 10");
        if (!clean_name(body, name)) return send_error(c, 400, "Enter a name for the booking");
        int id = theatre_auto_allocate(&theatre, name, count, &split, err, sizeof err);
        if (id < 0) return send_error(c, 400, err);
        send_theatre(c, id, split);
    } else if (!strcmp(method, "DELETE") && !strncmp(path, "/api/booking/", 13)) {
        char *end;
        long id = strtol(path + 13, &end, 10);
        if (end == path + 13 || *end != '\0') return send_error(c, 400, "Booking not found");
        if (theatre_cancel(&theatre, (int)id, err, sizeof err) < 0) return send_error(c, 400, err);
        send_theatre(c, 0, 0);
    } else if (!strcmp(method, "POST") && !strcmp(path, "/api/reset")) {
        theatre_reset(&theatre);
        send_theatre(c, 0, 0);
    } else {
        send_error(c, 404, "Not found");
    }
}

/* ---------- static files from ./public ---------- */

static const char *content_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (!strcmp(dot, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(dot, ".css")) return "text/css; charset=utf-8";
    if (!strcmp(dot, ".js")) return "application/javascript; charset=utf-8";
    if (!strcmp(dot, ".json")) return "application/json";
    if (!strcmp(dot, ".svg")) return "image/svg+xml";
    if (!strcmp(dot, ".png")) return "image/png";
    if (!strcmp(dot, ".ico")) return "image/x-icon";
    return "application/octet-stream";
}

static void serve_static(sock_t c, const char *target) {
    char path[256], full[300];
    size_t n = 0;
    while (target[n] && target[n] != '?' && target[n] != '#' && n < sizeof path - 1) {
        path[n] = target[n];
        n++;
    }
    path[n] = '\0';
    if (!strcmp(path, "/")) strcpy(path, "/index.html");
    if (path[0] != '/' || strstr(path, "..") || strchr(path, '\\'))
        return send_response(c, 404, "text/plain", "Not found", 9);

    snprintf(full, sizeof full, "public%s", path);
    FILE *f = fopen(full, "rb");
    if (!f) return send_response(c, 404, "text/plain", "Not found", 9);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size < 0 || size > 10 * 1024 * 1024) {
        fclose(f);
        return send_response(c, 404, "text/plain", "Not found", 9);
    }
    char *data = malloc((size_t)size + 1);
    if (!data) { fclose(f); return send_response(c, 404, "text/plain", "Not found", 9); }
    size_t got = fread(data, 1, (size_t)size, f);
    fclose(f);
    send_response(c, 200, content_type(path), data, got);
    free(data);
}

/* ---------- reading one HTTP request ---------- */

static int ci_prefix(const char *s, const char *prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*s) != *prefix) return 0;
        s++;
        prefix++;
    }
    return 1;
}

static size_t content_length(const char *headers, const char *end) {
    const char *p = strstr(headers, "\r\n");
    while (p && p < end) {
        p += 2;
        if (ci_prefix(p, "content-length:")) return (size_t)strtoul(p + 15, NULL, 10);
        p = strstr(p, "\r\n");
    }
    return 0;
}

static void handle_client(sock_t c) {
    static char req[REQ_MAX];
    size_t len = 0, need = 0;
    char *he = NULL;

    while (len < REQ_MAX - 1) {
        int n = (int)recv(c, req + len, (int)(REQ_MAX - 1 - len), 0);
        if (n <= 0) break;
        len += (size_t)n;
        req[len] = '\0';
        if (!he) {
            he = strstr(req, "\r\n\r\n");
            if (he) {
                need = (size_t)(he - req) + 4 + content_length(req, he);
                if (need > REQ_MAX - 1) return send_error(c, 400, "Request too large");
            }
        }
        if (he && len >= need) break;
    }
    if (!he) return;

    char method[8], target[1024];
    if (sscanf(req, "%7s %1023s", method, target) != 2) return;
    const char *body = he + 4;

    if (!strncmp(target, "/api/", 5)) handle_api(c, method, target, body);
    else if (!strcmp(method, "GET")) serve_static(c, target);
    else send_response(c, 404, "text/plain", "Not found", 9);
}

int main(void) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    theatre_init(&theatre);

    const char *env = getenv("PORT");
    int port = env ? atoi(env) : 3000;
    if (port <= 0) port = 3000;

    sock_t srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) { perror("socket"); return 1; }
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) < 0) {
        fprintf(stderr, "Could not use port %d. Is another program using it?\n", port);
        return 1;
    }
    listen(srv, 16);
    printf("Theatre running on http://localhost:%d\n", port);
    fflush(stdout);

    for (;;) {
        sock_t c = accept(srv, NULL, NULL);
        if (c == INVALID_SOCKET) continue;
#ifdef _WIN32
        DWORD ms = 5000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof ms);
#else
        struct timeval tv = {5, 0};
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
        handle_client(c);
        CLOSESOCK(c);
    }
}
